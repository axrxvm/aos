/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_filesystem.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <fs/vfs.h>
#include <fs/simplefs.h>
#include <fs/fat32.h>
#include <dev/ata.h>
#include <user.h>
#include <editor.h>
#include <memory.h>
#include <vmm.h>
#include <shell.h>
#include <fs_layout.h>
#include <partition.h>
#include <boot_info.h>
#include <multiboot.h>
#include <elf.h>

// Forward declaration
extern void kprint(const char *str);

#define INSTALL_ALIGN_SECTORS      2048U    // 1 MiB alignment
#define INSTALL_MIN_BOOT_SECTORS   65536U   // 32 MiB
#define INSTALL_MAX_BOOT_SECTORS   131072U  // 64 MiB
#define INSTALL_MIN_DATA_SECTORS   16384U   // 8 MiB
#define INSTALL_SECTOR_SIZE        512U
#define INSTALL_STAGE2_OFFSET      1U
#define INSTALL_MBR_MARKER         "ABLMBR1!"
#define INSTALL_STAGE2_MARKER      "ABLCFG2!"
#define INSTALL_STAGE2_MAGIC       0x32474643U
#define INSTALL_STAGE2_BUFFER_ADDR 0x00800000U
#define INSTALL_STAGE2_MAX_SEGMENTS 8U

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sectors;
} mbr_partition_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t paddr;
    uint32_t offset;
    uint32_t filesz;
    uint32_t memsz;
} install_stage2_segment_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t stage2_lba;
    uint32_t stage2_sectors;
    uint32_t kernel_lba;
    uint32_t kernel_sectors;
    uint32_t entry;
    uint32_t kernel_buffer;
    uint32_t segment_count;
    install_stage2_segment_t segments[INSTALL_STAGE2_MAX_SEGMENTS];
} install_stage2_runtime_cfg_t;

typedef struct __attribute__((packed)) {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} install_elf64_header_t;

typedef struct __attribute__((packed)) {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} install_elf64_program_header_t;

extern const uint8_t _binary_abl_mbr_bin_start[];
extern const uint8_t _binary_abl_mbr_bin_end[];
extern const uint8_t _binary_abl_stage2_bin_start[];
extern const uint8_t _binary_abl_stage2_bin_end[];

static uint32_t install_sectors_for_size(uint32_t size_bytes) {
    if (size_bytes == 0) {
        return 0;
    }
    return (size_bytes + INSTALL_SECTOR_SIZE - 1U) / INSTALL_SECTOR_SIZE;
}

static uint8_t* install_find_marker(uint8_t* data, uint32_t data_size, const char* marker, uint32_t marker_len) {
    if (!data || !marker || marker_len == 0 || data_size < marker_len) {
        return NULL;
    }

    for (uint32_t i = 0; i <= data_size - marker_len; i++) {
        if (memcmp(data + i, marker, marker_len) == 0) {
            return data + i;
        }
    }

    return NULL;
}

static int install_write_buffer_to_disk(uint32_t start_lba, const uint8_t* data, uint32_t data_size) {
    if (!data || data_size == 0) {
        return -1;
    }

    uint32_t sectors = install_sectors_for_size(data_size);
    uint8_t sector_buf[INSTALL_SECTOR_SIZE];

    for (uint32_t i = 0; i < sectors; i++) {
        memset(sector_buf, 0, sizeof(sector_buf));
        uint32_t src_offset = i * INSTALL_SECTOR_SIZE;
        uint32_t remaining = data_size - src_offset;
        uint32_t to_copy = remaining > INSTALL_SECTOR_SIZE ? INSTALL_SECTOR_SIZE : remaining;
        memcpy(sector_buf, data + src_offset, to_copy);

        if (ata_write_sectors(start_lba + i, 1, sector_buf) != 0) {
            return -1;
        }
    }

    return 0;
}

static int install_build_stage2_cfg(const uint8_t* kernel_blob,
                                    uint32_t kernel_blob_size,
                                    install_stage2_runtime_cfg_t* out_cfg) {
    if (!kernel_blob || kernel_blob_size < 64 || !out_cfg) {
        return -1;
    }

    if (kernel_blob[0] != 0x7F || kernel_blob[1] != 'E' ||
        kernel_blob[2] != 'L' || kernel_blob[3] != 'F') {
        return -1;
    }

    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->magic = INSTALL_STAGE2_MAGIC;
    out_cfg->kernel_buffer = INSTALL_STAGE2_BUFFER_ADDR;

    uint8_t elf_class = kernel_blob[4];
    uint32_t segment_count = 0;

    if (elf_class == 1) {
        if (kernel_blob_size < sizeof(elf32_header_t)) {
            return -1;
        }

        const elf32_header_t* eh = (const elf32_header_t*)kernel_blob;
        uint64_t ph_bytes = (uint64_t)eh->e_phentsize * (uint64_t)eh->e_phnum;
        if ((uint64_t)eh->e_phoff + ph_bytes > kernel_blob_size) {
            return -1;
        }

        out_cfg->entry = eh->e_entry;
        for (uint16_t i = 0; i < eh->e_phnum; i++) {
            const uint8_t* ph_ptr = kernel_blob + eh->e_phoff + ((uint32_t)i * eh->e_phentsize);
            const elf32_program_header_t* ph = (const elf32_program_header_t*)ph_ptr;

            if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
                continue;
            }
            if ((uint64_t)ph->p_offset + (uint64_t)ph->p_filesz > kernel_blob_size) {
                return -1;
            }
            if (segment_count >= INSTALL_STAGE2_MAX_SEGMENTS) {
                return -1;
            }

            uint32_t paddr = ph->p_paddr ? ph->p_paddr : ph->p_vaddr;
            out_cfg->segments[segment_count].paddr = paddr;
            out_cfg->segments[segment_count].offset = ph->p_offset;
            out_cfg->segments[segment_count].filesz = ph->p_filesz;
            out_cfg->segments[segment_count].memsz = ph->p_memsz;
            segment_count++;
        }
    } else if (elf_class == 2) {
        if (kernel_blob_size < sizeof(install_elf64_header_t)) {
            return -1;
        }

        const install_elf64_header_t* eh = (const install_elf64_header_t*)kernel_blob;
        uint64_t ph_bytes = (uint64_t)eh->e_phentsize * (uint64_t)eh->e_phnum;
        if (eh->e_phoff + ph_bytes > kernel_blob_size) {
            return -1;
        }
        if (eh->e_entry > 0xFFFFFFFFULL) {
            return -1;
        }

        out_cfg->entry = (uint32_t)eh->e_entry;
        for (uint16_t i = 0; i < eh->e_phnum; i++) {
            const uint8_t* ph_ptr = kernel_blob + (uint32_t)eh->e_phoff + ((uint32_t)i * eh->e_phentsize);
            const install_elf64_program_header_t* ph = (const install_elf64_program_header_t*)ph_ptr;

            if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
                continue;
            }
            if (ph->p_offset + ph->p_filesz > kernel_blob_size) {
                return -1;
            }
            if (ph->p_paddr > 0xFFFFFFFFULL || ph->p_vaddr > 0xFFFFFFFFULL ||
                ph->p_filesz > 0xFFFFFFFFULL || ph->p_memsz > 0xFFFFFFFFULL ||
                ph->p_offset > 0xFFFFFFFFULL) {
                return -1;
            }
            if (segment_count >= INSTALL_STAGE2_MAX_SEGMENTS) {
                return -1;
            }

            uint32_t paddr = ph->p_paddr ? (uint32_t)ph->p_paddr : (uint32_t)ph->p_vaddr;
            out_cfg->segments[segment_count].paddr = paddr;
            out_cfg->segments[segment_count].offset = (uint32_t)ph->p_offset;
            out_cfg->segments[segment_count].filesz = (uint32_t)ph->p_filesz;
            out_cfg->segments[segment_count].memsz = (uint32_t)ph->p_memsz;
            segment_count++;
        }
    } else {
        return -1;
    }

    if (segment_count == 0) {
        return -1;
    }

    out_cfg->segment_count = segment_count;
    return 0;
}

static int install_patch_stage2_binary(uint8_t* stage2_binary,
                                       uint32_t stage2_binary_size,
                                       const install_stage2_runtime_cfg_t* cfg) {
    if (!stage2_binary || !cfg) {
        return -1;
    }

    uint8_t* marker = install_find_marker(stage2_binary, stage2_binary_size,
                                          INSTALL_STAGE2_MARKER, 8);
    if (!marker) {
        return -1;
    }

    uint32_t offset = (uint32_t)(marker - stage2_binary);
    if (offset + 8U + sizeof(*cfg) > stage2_binary_size) {
        return -1;
    }

    memcpy(marker + 8, cfg, sizeof(*cfg));
    return 0;
}

static int write_install_mbr(uint32_t boot_start,
                             uint32_t boot_sectors,
                             uint32_t data_start,
                             uint32_t data_sectors,
                             uint32_t stage2_lba,
                             uint16_t stage2_sectors) {
    uint8_t mbr[512];
    memset(mbr, 0, sizeof(mbr));

    uint32_t mbr_blob_size = (uint32_t)(_binary_abl_mbr_bin_end - _binary_abl_mbr_bin_start);
    if (mbr_blob_size < sizeof(mbr)) {
        return -1;
    }
    memcpy(mbr, _binary_abl_mbr_bin_start, sizeof(mbr));

    uint8_t* marker = install_find_marker(mbr, 446U, INSTALL_MBR_MARKER, 8);
    if (!marker) {
        return -1;
    }
    memcpy(marker + 8, &stage2_lba, sizeof(stage2_lba));
    memcpy(marker + 12, &stage2_sectors, sizeof(stage2_sectors));

    mbr_partition_entry_t* entries = (mbr_partition_entry_t*)&mbr[446];
    memset(entries, 0, sizeof(mbr_partition_entry_t) * 4U);

    // Partition 1: aOS boot payload partition.
    entries[0].status = 0x80;
    entries[0].chs_first[0] = 0xFE;
    entries[0].chs_first[1] = 0xFF;
    entries[0].chs_first[2] = 0xFF;
    entries[0].type = 0xA0;
    entries[0].chs_last[0] = 0xFE;
    entries[0].chs_last[1] = 0xFF;
    entries[0].chs_last[2] = 0xFF;
    entries[0].lba_first = boot_start;
    entries[0].sectors = boot_sectors;

    // Partition 2: aOS data partition (SimpleFS).
    entries[1].status = 0x00;
    entries[1].chs_first[0] = 0xFE;
    entries[1].chs_first[1] = 0xFF;
    entries[1].chs_first[2] = 0xFF;
    entries[1].type = 0x83;
    entries[1].chs_last[0] = 0xFE;
    entries[1].chs_last[1] = 0xFF;
    entries[1].chs_last[2] = 0xFF;
    entries[1].lba_first = data_start;
    entries[1].sectors = data_sectors;

    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    return ata_write_sectors(0, 1, mbr);
}

static int find_installer_kernel_module(const uint8_t** out_data, uint32_t* out_size) {
    if (!out_data || !out_size) {
        return -1;
    }

    const multiboot_info_t* mbi = boot_info_get_multiboot();
    if (!mbi || mbi->mods_count == 0 || mbi->mods_addr == 0) {
        return -1;
    }

    const multiboot_module_t* mods = (const multiboot_module_t*)(uintptr_t)mbi->mods_addr;
    int selected_index = -1;

    for (uint32_t i = 0; i < mbi->mods_count; i++) {
        uint32_t start = mods[i].mod_start;
        uint32_t end = mods[i].mod_end;
        if (end <= start) {
            continue;
        }

        if (selected_index < 0) {
            selected_index = (int)i;
        }

        const char* cmdline = (const char*)(uintptr_t)mods[i].cmdline;
        if (cmdline && strstr(cmdline, "aos-installer-kernel")) {
            selected_index = (int)i;
            break;
        }
    }

    if (selected_index < 0) {
        return -1;
    }

    *out_data = (const uint8_t*)(uintptr_t)mods[selected_index].mod_start;
    *out_size = mods[selected_index].mod_end - mods[selected_index].mod_start;
    return 0;
}

// VFS test command
static void cmd_test_fs(const char* args) {
    (void)args;
    char buf[32];
    
    kprint("Testing VFS with ramfs...");
    kprint("");
    
    // Test 1: Create a directory
    kprint("Creating directory /test...");
    int ret = sys_mkdir("/test");
    if (ret == VFS_OK) {
        kprint("  [OK] Directory created");
    } else {
        vga_puts("  [FAIL] Error: "); itoa(ret, buf, 10); kprint(buf);
        return;
    }
    
    // Test 2: Create and write to a file
    kprint("Creating file /test/hello.txt...");
    int fd = sys_open("/test/hello.txt", O_CREAT | O_WRONLY);
    if (fd >= 0) {
        vga_puts("  [OK] File opened (fd="); itoa(fd, buf, 10); vga_puts(buf); kprint(")");
        
        const char* test_data = "Hello from aOS filesystem!";
        int bytes = sys_write(fd, test_data, strlen(test_data));
        vga_puts("  [OK] Wrote "); itoa(bytes, buf, 10); vga_puts(buf); kprint(" bytes");
        
        sys_close(fd);
        kprint("  [OK] File closed");
    } else {
        vga_puts("  [FAIL] Error: "); itoa(fd, buf, 10); kprint(buf);
        return;
    }
    
    // Test 3: Read the file back
    kprint("Reading file /test/hello.txt...");
    fd = sys_open("/test/hello.txt", O_RDONLY);
    if (fd >= 0) {
        char read_buf[128] = {0};
        int bytes = sys_read(fd, read_buf, sizeof(read_buf) - 1);
        vga_puts("  [OK] Read "); itoa(bytes, buf, 10); vga_puts(buf); kprint(" bytes");
        vga_puts("  Content: "); kprint(read_buf);
        
        sys_close(fd);
        kprint("  [OK] File closed");
    } else {
        vga_puts("  [FAIL] Error: "); itoa(fd, buf, 10); kprint(buf);
        return;
    }
    
    // Test 4: List root directory
    kprint("Listing root directory /...");
    fd = sys_open("/", O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        dirent_t dirent;
        int count = 0;
        while (sys_readdir(fd, &dirent) == VFS_OK) {
            vga_puts("  "); vga_puts(dirent.name);
            vga_puts(" (inode="); itoa(dirent.inode, buf, 10); vga_puts(buf);
            vga_puts(", type="); itoa(dirent.type, buf, 10); vga_puts(buf); kprint(")");
            count++;
        }
        vga_puts("  [OK] Found "); itoa(count, buf, 10); vga_puts(buf); kprint(" entries");
        sys_close(fd);
    } else {
        vga_puts("  [FAIL] Error: "); itoa(fd, buf, 10); kprint(buf);
        return;
    }
    
    kprint("");
    kprint("VFS test completed successfully!");
}

static void cmd_lst(const char* args) {
    char buf[32];
    const char* path = args && *args ? args : ".";
    
    int fd = sys_open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("lst: cannot access '");
        vga_puts(path);
        vga_puts("': ");
        if (fd == VFS_ERR_NOTFOUND) {
            vga_puts("No such file or directory");
        } else if (fd == VFS_ERR_NOTDIR) {
            vga_puts("Not a directory");
        } else {
            vga_puts("Error ");
            itoa(fd, buf, 10);
            vga_puts(buf);
        }
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        return;
    }
    
    dirent_t dirent;
    int count = 0;
    while (sys_readdir(fd, &dirent) == VFS_OK) {
        if (shell_is_cancelled()) {
            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            kprint("\nCommand cancelled.");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            sys_close(fd);
            return;
        }
        
        if (dirent.type == VFS_DIRECTORY) {
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        } else {
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
        vga_puts(dirent.name);
        if (dirent.type == VFS_DIRECTORY) {
            vga_puts("/");
        }
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        count++;
    }
    
    if (count == 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        kprint("(empty directory)");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    sys_close(fd);
}

static void cmd_view(const char* args) {
    char buf[32];
    
    if (!args || !*args) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("view: missing file operand");
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        kprint("Usage: view <filename>");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    
    int fd = sys_open(args, O_RDONLY);
    if (fd < 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("view: '"); vga_puts(args); vga_puts("': ");
        if (fd == VFS_ERR_NOTFOUND) {
            vga_puts("No such file or directory");
        } else if (fd == VFS_ERR_ISDIR) {
            vga_puts("Is a directory");
        } else {
            vga_puts("Error "); itoa(fd, buf, 10); vga_puts(buf);
        }
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        return;
    }
    
    char read_buf[256];
    int bytes;
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    while ((bytes = sys_read(fd, read_buf, sizeof(read_buf) - 1)) > 0) {
        if (shell_is_cancelled()) {
            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            kprint("\nCommand cancelled.");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            sys_close(fd);
            return;
        }
        read_buf[bytes] = '\0';
        vga_puts(read_buf);
    }
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    sys_close(fd);
}

static void cmd_create(const char* args) {
    char buf[32];
    
    if (!args || !*args) {
        kprint("create: missing file operand");
        kprint("Usage: create <filename> [--empty]");
        return;
    }
    
    const char* space = args;
    while (*space && *space != ' ') {
        space++;
    }
    
    char filename[256];
    uint32_t name_len;
    int empty_mode = 0;
    
    if (*space != '\0') {
        const char* flag = space;
        while (*flag && *flag == ' ') {
            flag++;
        }
        
        if (strcmp(flag, "--empty") == 0) {
            empty_mode = 1;
            name_len = space - args;
        } else {
            name_len = strlen(args);
        }
    } else {
        name_len = strlen(args);
    }
    
    if (name_len >= sizeof(filename)) {
        kprint("create: filename too long");
        return;
    }
    
    for (uint32_t i = 0; i < name_len; i++) {
        filename[i] = args[i];
    }
    filename[name_len] = '\0';
    
    if (empty_mode) {
        int fd_check = sys_open(filename, O_RDONLY);
        if (fd_check >= 0) {
            sys_close(fd_check);
            vga_puts("Touched: "); kprint(filename);
            return;
        }
    }
    
    int fd = sys_open(filename, O_CREAT | O_WRONLY);
    if (fd < 0) {
        vga_puts("create: cannot create '"); vga_puts(filename); vga_puts("': ");
        if (fd == VFS_ERR_EXISTS) {
            kprint("File already exists");
        } else if (fd == VFS_ERR_NOTFOUND) {
            kprint("Parent directory not found");
        } else if (fd == VFS_ERR_NOSPACE) {
            kprint("No space left");
        } else if (fd == VFS_ERR_IO) {
            kprint("I/O error");
        } else {
            vga_puts("Error code "); 
            if (fd < 0) {
                vga_putc('-');
                itoa(-fd, buf, 10);
            } else {
                itoa(fd, buf, 10);
            }
            kprint(buf);
        }
        return;
    }
    
    sys_close(fd);
    vga_puts("Created file: "); kprint(filename);
}

static void cmd_write(const char* args) {
    char buf[32];
    
    if (!args || !*args) {
        kprint("write: missing file operand");
        kprint("Usage: write <filename> <content>");
        return;
    }
    
    const char* space = args;
    while (*space && *space != ' ') {
        space++;
    }
    
    if (*space == '\0') {
        kprint("write: missing content");
        kprint("Usage: write <filename> <content>");
        return;
    }
    
    char filename[256];
    uint32_t name_len = space - args;
    if (name_len >= sizeof(filename)) {
        kprint("write: filename too long");
        return;
    }
    for (uint32_t i = 0; i < name_len; i++) {
        filename[i] = args[i];
    }
    filename[name_len] = '\0';
    
    const char* content = space;
    while (*content && *content == ' ') {
        content++;
    }
    
    if (*content == '\0') {
        kprint("write: missing content");
        return;
    }
    
    int fd = sys_open(filename, O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0) {
        vga_puts("write: cannot open '"); vga_puts(filename); vga_puts("': ");
        if (fd == VFS_ERR_NOTFOUND) {
            kprint("Parent directory not found");
        } else if (fd == VFS_ERR_ISDIR) {
            kprint("Is a directory");
        } else {
            vga_puts("Error "); itoa(fd, buf, 10); kprint(buf);
        }
        return;
    }
    
    int content_len = strlen(content);
    int bytes = sys_write(fd, content, content_len);
    
    if (bytes < 0) {
        vga_puts("write: write error: "); itoa(bytes, buf, 10); kprint(buf);
    } else {
        vga_puts("Wrote "); itoa(bytes, buf, 10); vga_puts(buf); 
        vga_puts(" bytes to "); kprint(filename);
    }
    
    sys_close(fd);
}

static int rm_recursive(const char* path) {
    char buf[32];
    stat_t file_stat;
    int stat_ret = sys_stat(path, &file_stat);
    
    if (stat_ret != VFS_OK) {
        return stat_ret;
    }
    
    if ((file_stat.st_mode & 0xF000) != 0x4000) {
        return sys_unlink(path);
    }
    
    int fd = sys_open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return fd;
    }
    
    dirent_t dirent;
    char child_path[512];
    
    while (sys_readdir(fd, &dirent) == VFS_OK) {
        if (strcmp(dirent.name, ".") == 0 || strcmp(dirent.name, "..") == 0) {
            continue;
        }
        
        uint32_t path_len = strlen(path);
        uint32_t name_len = strlen(dirent.name);
        
        if (path_len + name_len + 2 > 511) {
            sys_close(fd);
            return VFS_ERR_INVALID;
        }
        
        strcpy(child_path, path);
        if (path_len > 0 && path[path_len - 1] != '/') {
            child_path[path_len] = '/';
            child_path[path_len + 1] = '\0';
        }
        strcat(child_path, dirent.name);
        
        int ret = rm_recursive(child_path);
        if (ret != VFS_OK) {
            vga_puts("rm: failed to remove '"); vga_puts(child_path); vga_puts("': Error ");
            itoa(ret, buf, 10); kprint(buf);
            sys_close(fd);
            return ret;
        }
    }
    
    sys_close(fd);
    return sys_rmdir(path);
}

static void cmd_rm(const char* args) {
    char buf[32];
    
    if (!args || !*args) {
        kprint("rm: missing operand");
        kprint("Usage: rm [--force] <file|directory>");
        return;
    }
    
    int force = 0;
    const char* path = args;
    
    if (strncmp(args, "--force", 7) == 0) {
        force = 1;
        path = args + 7;
        while (*path && *path == ' ') {
            path++;
        }
        if (*path == '\0') {
            kprint("rm: missing operand after --force");
            kprint("Usage: rm [--force] <file|directory>");
            return;
        }
    }
    
    stat_t file_stat;
    int stat_ret = sys_stat(path, &file_stat);
    
    if (stat_ret != VFS_OK) {
        vga_puts("rm: cannot access '"); vga_puts(path); vga_puts("': ");
        if (stat_ret == VFS_ERR_NOTFOUND) {
            kprint("No such file or directory");
        } else {
            vga_puts("Error "); itoa(stat_ret, buf, 10); kprint(buf);
        }
        return;
    }
    
    int ret;
    if (force) {
        ret = rm_recursive(path);
    } else {
        if ((file_stat.st_mode & 0xF000) == 0x4000) {
            ret = sys_rmdir(path);
        } else {
            ret = sys_unlink(path);
        }
    }
    
    if (ret == VFS_OK) {
        vga_puts("Removed: "); kprint(path);
    } else {
        vga_puts("rm: cannot remove '"); vga_puts(path); vga_puts("': ");
        if (ret == VFS_ERR_NOTFOUND) {
            kprint("No such file or directory");
        } else if (ret == VFS_ERR_NOTEMPTY) {
            kprint("Directory not empty (use --force to remove recursively)");
        } else if (ret == VFS_ERR_PERM) {
            kprint("Permission denied");
        } else {
            vga_puts("Error "); itoa(ret, buf, 10); kprint(buf);
        }
    }
}

static void cmd_mkfld(const char* args) {
    char buf[32];
    
    if (!args || !*args) {
        kprint("mkfld: missing directory operand");
        kprint("Usage: mkfld <dirname>");
        return;
    }
    
    int ret = sys_mkdir(args);
    if (ret == VFS_OK) {
        vga_puts("Created directory: "); kprint(args);
    } else {
        vga_puts("mkfld: cannot create directory '"); vga_puts(args); vga_puts("': ");
        if (ret == VFS_ERR_EXISTS) {
            kprint("File or directory already exists");
        } else if (ret == VFS_ERR_NOTFOUND) {
            kprint("Parent directory not found");
        } else if (ret == VFS_ERR_NOSPACE) {
            kprint("No space left");
        } else {
            vga_puts("Error "); itoa(ret, buf, 10); kprint(buf);
        }
    }
}

static void cmd_go(const char* args) {
    char buf[32];
    
    if (!args || !*args) {
        kprint("go: missing directory operand");
        kprint("Usage: go <directory>");
        return;
    }
    
    if (!user_is_admin()) {
        session_t* session = user_get_session();
        if (!session || !session->user) {
            kprint("Error: Not logged in");
            return;
        }
        
        char* resolved_path = vfs_normalize_path(args);
        if (!resolved_path) {
            vga_set_color(0x0C);
            vga_puts("go: Failed to resolve path\n");
            vga_set_color(0x0F);
            return;
        }
        
        const char* home_dir = session->user->home_dir;
        uint32_t home_len = strlen(home_dir);
        
        int is_within_home = 0;
        if (strcmp(resolved_path, home_dir) == 0) {
            is_within_home = 1;
        } else if (strncmp(resolved_path, home_dir, home_len) == 0) {
            if (resolved_path[home_len] == '/') {
                is_within_home = 1;
            }
        }
        
        if (!is_within_home) {
            vga_set_color(0x0C);
            vga_puts("go: Permission denied - non-admin users cannot leave home directory\n");
            vga_set_color(0x0F);
            kfree(resolved_path);
            return;
        }
        
        kfree(resolved_path);
    }
    
    int ret = vfs_chdir(args);
    if (ret == VFS_OK) {
        const char* cwd = vfs_getcwd();
        vga_puts("Changed directory to: ");
        kprint(cwd ? cwd : "/");
    } else {
        vga_puts("go: cannot change directory to '"); vga_puts(args); vga_puts("': ");
        if (ret == VFS_ERR_NOTFOUND) {
            kprint("No such file or directory");
        } else if (ret == VFS_ERR_NOTDIR) {
            kprint("Not a directory");
        } else {
            vga_puts("Error "); itoa(ret, buf, 10); kprint(buf);
        }
    }
}

static void cmd_pwd(const char* args) {
    (void)args;
    const char* cwd = vfs_getcwd();
    kprint(cwd ? cwd : "/");
}

static void cmd_disk_info(const char* args) {
    (void)args;
    char buf[32];
    
    if (ata_drive_available()) {
        kprint("ATA Drive Status: Available");
        kprint("Disk operations: Enabled");
        kprint("Filesystem: SimpleFS (if mounted)");
        kprint("");
        
        simplefs_superblock_t stats;
        if (simplefs_get_stats(&stats) == 0) {
            kprint("=== SimpleFS Statistics ===");
            
            vga_puts("Total Blocks: ");
            itoa(stats.total_blocks, buf, 10);
            kprint(buf);
            
            vga_puts("Free Blocks: ");
            itoa(stats.free_blocks, buf, 10);
            kprint(buf);
            
            vga_puts("Used Blocks: ");
            itoa(stats.total_blocks - stats.free_blocks, buf, 10);
            kprint(buf);
            
            vga_puts("Total Inodes: ");
            itoa(stats.total_inodes, buf, 10);
            kprint(buf);
            
            vga_puts("Free Inodes: ");
            itoa(stats.free_inodes, buf, 10);
            kprint(buf);
            
            vga_puts("Used Inodes: ");
            itoa(stats.total_inodes - stats.free_inodes, buf, 10);
            kprint(buf);
            
            uint32_t total_mb = (stats.total_blocks * stats.block_size) / (1024 * 1024);
            uint32_t used_mb = ((stats.total_blocks - stats.free_blocks) * stats.block_size) / (1024 * 1024);
            uint32_t free_mb = (stats.free_blocks * stats.block_size) / (1024 * 1024);
            
            vga_puts("Total Size: ");
            itoa(total_mb, buf, 10);
            vga_puts(buf);
            kprint(" MB");
            
            vga_puts("Used Space: ");
            itoa(used_mb, buf, 10);
            vga_puts(buf);
            kprint(" MB");
            
            vga_puts("Free Space: ");
            itoa(free_mb, buf, 10);
            vga_puts(buf);
            kprint(" MB");
        } else {
            kprint("Could not retrieve filesystem statistics");
        }
    } else {
        kprint("ATA Drive Status: Not Available");
        kprint("Using RAM-based filesystem (ramfs)");
    }
}

static void cmd_format(const char* args) {
    if (!ata_drive_available()) {
        kprint("Error: No ATA drive available to format");
        return;
    }
    
    uint32_t total_sectors = ata_get_sector_count();
    uint32_t disk_mb = (total_sectors * 512) / (1024 * 1024);
    uint32_t format_start = 0;
    uint32_t format_sectors = total_sectors;
    const char* target_desc = "whole disk";
    
    char buf[128];
    char temp[32];
    
    // Parse filesystem type argument
    const char* fstype = NULL;
    if (args && *args) {
        // Skip whitespace
        while (*args == ' ' || *args == '\t') args++;
        if (*args) {
            fstype = args;
        }
    }
    
    // Show options if no argument provided
    if (!fstype || *fstype == '\0') {
        kprint("=== Disk Format Utility ===");
        kprint("");
        strcpy(buf, "Disk Size: ");
        itoa(disk_mb, temp, 10);
        strcat(buf, temp);
        strcat(buf, " MB (");
        itoa(total_sectors, temp, 10);
        strcat(buf, temp);
        strcat(buf, " sectors)");
        kprint(buf);
        kprint("");
        kprint("Available Filesystem Types:");
        kprint("  simplefs  - aOS native filesystem (simple, fast)");
        kprint("  fat32     - FAT32 filesystem (compatible with Windows/Linux/macOS, slow)");
        kprint("");
        kprint("Usage: format <filesystem-type>");
        kprint("Example: format simplefs");
        kprint("Example: format fat32");
        kprint("If installed layout exists, simplefs defaults to the data partition.");
        kprint("");
        kprint("WARNING: Formatting will erase ALL data on the disk!");
        return;
    }
    
    // Validate filesystem type
    int result = -1;
    int is_valid = 0;
    
    if (strcmp(fstype, "simplefs") == 0) {
        is_valid = 1;
    } else if (strcmp(fstype, "fat32") == 0) {
        is_valid = 1;
    }
    
    if (!is_valid) {
        kprint("Error: Unknown filesystem type");
        kprint("Supported types: simplefs, fat32");
        kprint("Use 'format' without arguments to see options");
        return;
    }

    // Prefer partition targets when an installed layout exists.
    if (strcmp(fstype, "simplefs") == 0) {
        int part_id = partition_find_first_by_type_and_fs(PART_TYPE_DATA, PART_FS_SIMPLEFS);
        if (part_id < 0) {
            part_id = partition_find_first_by_type(PART_TYPE_DATA);
        }
        if (part_id >= 0) {
            partition_t* part = partition_get(part_id);
            if (part && part->sector_count > 0) {
                format_start = part->start_sector;
                format_sectors = part->sector_count;
                target_desc = "installed data partition";
            }
        }
    } else if (strcmp(fstype, "fat32") == 0) {
        int part_id = partition_find_first_by_type_and_fs(PART_TYPE_SYSTEM, PART_FS_FAT32);
        if (part_id < 0) {
            part_id = partition_find_first_by_type(PART_TYPE_SYSTEM);
        }
        if (part_id >= 0) {
            partition_t* part = partition_get(part_id);
            if (part && part->sector_count > 0) {
                format_start = part->start_sector;
                format_sectors = part->sector_count;
                target_desc = "system partition";
            }
        }
    }
    
    // Display warning and format information
    kprint("=== WARNING: FORMAT DISK ===");
    kprint("");
    kprint("This will ERASE ALL DATA on the selected target!");
    kprint("");
    strcpy(buf, "Target:         ");
    strcat(buf, target_desc);
    kprint(buf);
    
    strcpy(buf, "Start LBA:      ");
    itoa(format_start, temp, 10);
    strcat(buf, temp);
    kprint(buf);
    
    strcpy(buf, "Sectors:        ");
    itoa(format_sectors, temp, 10);
    strcat(buf, temp);
    kprint(buf);
    
    strcpy(buf, "Disk Size:      ");
    itoa(disk_mb, temp, 10);
    strcat(buf, temp);
    strcat(buf, " MB");
    kprint(buf);
    
    strcpy(buf, "Filesystem:     ");
    strcat(buf, fstype);
    kprint(buf);
    
    if (strcmp(fstype, "simplefs") == 0) {
        strcpy(buf, "Blocks:         ");
        itoa(format_sectors, temp, 10);
        strcat(buf, temp);
        kprint(buf);
    } else if (strcmp(fstype, "fat32") == 0) {
        strcpy(buf, "Sectors:        ");
        itoa(format_sectors, temp, 10);
        strcat(buf, temp);
        kprint(buf);
        
        if (format_start != 0) {
            strcpy(buf, "Volume Label:   AOS_BOOT");
        } else {
            strcpy(buf, "Volume Label:   aOS_DISK");
        }
        kprint(buf);
    }
    
    kprint("");
    kprint("Formatting...");
    
    // Perform format
    if (strcmp(fstype, "simplefs") == 0) {
        result = simplefs_format(format_start, format_sectors);
    } else if (strcmp(fstype, "fat32") == 0) {
        result = fat32_format(format_start,
                              format_sectors,
                              (format_start != 0) ? "AOS_BOOT" : "aOS_DISK");
    }
    
    kprint("");
    if (result == 0) {
        kprint("SUCCESS: Disk formatted successfully!");
        kprint("");
        kprint("The disk has been formatted with ");
        kprint(fstype);
        kprint("");
        kprint("Please reboot to mount the new filesystem.");
        kprint("Use 'reboot' command to restart the system.");
    } else {
        kprint("ERROR: Failed to format disk");
        kprint("Please check the disk and try again.");
    }
}

static void cmd_install(const char* args) {
    int force = 0;
    int running_local_mode = (fs_layout_get_mode() == FS_MODE_LOCAL);

    if (args && *args) {
        while (*args == ' ' || *args == '\t') {
            args++;
        }
        if (*args) {
            if (strcmp(args, "--force") == 0) {
                force = 1;
            } else {
                kprint("Usage: install [--force]");
                kprint("  --force: allow install while running from local disk mode");
                return;
            }
        }
    }

    if (!ata_drive_available()) {
        kprint("install: no ATA drive available");
        return;
    }

    if (running_local_mode && !force) {
        kprint("install: refused while running in LOCAL mode");
        kprint("Boot from ISO/ramfs mode, or use 'install --force'.");
        return;
    }

    if (running_local_mode && force) {
        kprint("install: running with --force in LOCAL mode.");
        kprint("Reinstalling while booted from disk can be destructive.");
        kprint("");
    }

    const uint8_t* kernel_blob = NULL;
    uint32_t kernel_blob_size = 0;
    if (find_installer_kernel_module(&kernel_blob, &kernel_blob_size) != 0 || kernel_blob_size == 0) {
        kprint("install: installer kernel payload not available");
        kprint("Boot from the installer ISO build that includes 'aos-installer-kernel' module.");
        return;
    }

    uint32_t mbr_blob_size = (uint32_t)(_binary_abl_mbr_bin_end - _binary_abl_mbr_bin_start);
    uint32_t stage2_blob_size = (uint32_t)(_binary_abl_stage2_bin_end - _binary_abl_stage2_bin_start);
    if (mbr_blob_size < 512U || stage2_blob_size == 0) {
        kprint("install: embedded bootloader payload missing");
        return;
    }

    uint32_t total_sectors = ata_get_sector_count();
    if (total_sectors == 0) {
        kprint("install: unable to detect drive size");
        return;
    }

    uint32_t boot_start = INSTALL_ALIGN_SECTORS;
    uint32_t boot_sectors = total_sectors / 4U;  // 25% of disk by default
    if (boot_sectors < INSTALL_MIN_BOOT_SECTORS) {
        boot_sectors = INSTALL_MIN_BOOT_SECTORS;
    } else if (boot_sectors > INSTALL_MAX_BOOT_SECTORS) {
        boot_sectors = INSTALL_MAX_BOOT_SECTORS;
    }

    if (boot_start + boot_sectors + INSTALL_MIN_DATA_SECTORS > total_sectors) {
        if (total_sectors <= boot_start + INSTALL_MIN_DATA_SECTORS) {
            kprint("install: disk too small for installation layout");
            return;
        }
        boot_sectors = total_sectors - boot_start - INSTALL_MIN_DATA_SECTORS;
    }

    if (boot_sectors < INSTALL_MIN_BOOT_SECTORS) {
        kprint("install: disk too small for required boot partition");
        return;
    }

    uint32_t data_start = boot_start + boot_sectors;
    uint32_t data_sectors = total_sectors - data_start;
    if (data_sectors < INSTALL_MIN_DATA_SECTORS) {
        kprint("install: not enough space for data partition");
        return;
    }

    uint32_t stage2_sectors = install_sectors_for_size(stage2_blob_size);
    uint32_t kernel_sectors = install_sectors_for_size(kernel_blob_size);
    uint32_t stage2_lba = boot_start + INSTALL_STAGE2_OFFSET;
    uint32_t kernel_lba = stage2_lba + stage2_sectors;
    uint32_t boot_end = boot_start + boot_sectors;
    if (kernel_lba + kernel_sectors > boot_end) {
        kprint("install: boot partition too small for loader + kernel payload");
        return;
    }
    if (stage2_sectors > 0xFFFFU) {
        kprint("install: stage2 loader too large");
        return;
    }

    install_stage2_runtime_cfg_t stage2_cfg;
    if (install_build_stage2_cfg(kernel_blob, kernel_blob_size, &stage2_cfg) != 0) {
        kprint("install: unsupported kernel ELF payload");
        return;
    }
    stage2_cfg.stage2_lba = stage2_lba;
    stage2_cfg.stage2_sectors = stage2_sectors;
    stage2_cfg.kernel_lba = kernel_lba;
    stage2_cfg.kernel_sectors = kernel_sectors;

    uint8_t* stage2_patched = (uint8_t*)kmalloc(stage2_blob_size);
    if (!stage2_patched) {
        kprint("install: out of memory while preparing stage2 payload");
        return;
    }
    memcpy(stage2_patched, _binary_abl_stage2_bin_start, stage2_blob_size);
    if (install_patch_stage2_binary(stage2_patched, stage2_blob_size, &stage2_cfg) != 0) {
        kfree(stage2_patched);
        kprint("install: failed to patch stage2 loader payload");
        return;
    }

    char buf[128];
    char tmp[32];

    kprint("=== aOS Installer ===");
    strcpy(buf, "Disk size: ");
    itoa((total_sectors * 512U) / (1024U * 1024U), tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " MB");
    kprint(buf);

    strcpy(buf, "Boot partition: start=");
    itoa(boot_start, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors, size=");
    itoa(boot_sectors, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors");
    kprint(buf);

    strcpy(buf, "Data partition: start=");
    itoa(data_start, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors, size=");
    itoa(data_sectors, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors");
    kprint(buf);

    strcpy(buf, "Stage2 loader: start=");
    itoa(stage2_lba, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors, size=");
    itoa(stage2_sectors, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors");
    kprint(buf);

    strcpy(buf, "Kernel payload: start=");
    itoa(kernel_lba, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors, size=");
    itoa(kernel_sectors, tmp, 10);
    strcat(buf, tmp);
    strcat(buf, " sectors");
    kprint(buf);
    kprint("");

    kprint("Writing MBR partition layout...");
    if (write_install_mbr(boot_start,
                          boot_sectors,
                          data_start,
                          data_sectors,
                          stage2_lba,
                          (uint16_t)stage2_sectors) != 0) {
        kfree(stage2_patched);
        kprint("install: failed to write MBR partition entries");
        return;
    }

    partition_clear();
    int boot_part_id = partition_create("aos-boot", PART_TYPE_SYSTEM, boot_start, boot_sectors);
    int data_part_id = partition_create("aos-data", PART_TYPE_DATA, data_start, data_sectors);
    if (boot_part_id < 0 || data_part_id < 0) {
        kfree(stage2_patched);
        kprint("install: failed to create aOS partition metadata");
        return;
    }

    partition_t* boot_part = partition_get(boot_part_id);
    partition_t* data_part = partition_get(data_part_id);
    if (!boot_part || !data_part) {
        kfree(stage2_patched);
        kprint("install: internal partition metadata error");
        return;
    }
    boot_part->active = 1;
    boot_part->filesystem_type = PART_FS_UNKNOWN;
    data_part->active = 0;
    data_part->filesystem_type = PART_FS_SIMPLEFS;

    if (partition_save_table() != 0) {
        kfree(stage2_patched);
        kprint("install: failed to save aOS partition table");
        return;
    }

    kprint("Formatting data partition as SimpleFS...");
    if (simplefs_format(data_start, data_sectors) != 0) {
        kfree(stage2_patched);
        kprint("install: failed to format data partition");
        return;
    }

    kprint("Installing ABL (aOS Bootloader) stages...");
    if (install_write_buffer_to_disk(stage2_lba, stage2_patched, stage2_blob_size) != 0) {
        kfree(stage2_patched);
        kprint("install: failed to write stage2 loader");
        return;
    }
    if (install_write_buffer_to_disk(kernel_lba, kernel_blob, kernel_blob_size) != 0) {
        kfree(stage2_patched);
        kprint("install: failed to write kernel payload");
        return;
    }
    kfree(stage2_patched);

    kprint("");
    kprint("SUCCESS: aOS disk layout installed.");
    kprint("Boot partition prepared with ABL stage1 (MBR) + stage2 loader.");
    kprint("Kernel payload installed to boot partition.");
    kprint("Data partition prepared with SimpleFS for persistent storage.");
    kprint("Reboot and set the machine to boot from disk.");
}

static void cmd_test_disk_write(const char* args) {
    (void)args;
    char buf[32];
    
    kprint("=== Disk Write Test ===");
    
    if (!ata_drive_available()) {
        kprint("Error: No ATA drive available");
        return;
    }
    
    kprint("Step 1: Opening /testfile.txt for writing...");
    int fd = sys_open("/testfile.txt", O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0) {
        vga_puts("FAILED: Cannot open file, error=");
        itoa(fd, buf, 10);
        kprint(buf);
        return;
    }
    vga_puts("SUCCESS: File opened, fd=");
    itoa(fd, buf, 10);
    kprint(buf);
    
    kprint("Step 2: Writing 'Hello Disk!' to file...");
    const char* test_data = "Hello Disk!";
    int bytes = sys_write(fd, test_data, strlen(test_data));
    if (bytes < 0) {
        vga_puts("FAILED: Write error=");
        itoa(bytes, buf, 10);
        kprint(buf);
        sys_close(fd);
        return;
    }
    vga_puts("SUCCESS: Wrote ");
    itoa(bytes, buf, 10);
    vga_puts(buf);
    kprint(" bytes");
    
    kprint("Step 3: Closing file...");
    sys_close(fd);
    kprint("SUCCESS: File closed");
    
    kprint("Step 4: Reading back from disk...");
    fd = sys_open("/testfile.txt", O_RDONLY);
    if (fd < 0) {
        vga_puts("FAILED: Cannot reopen file, error=");
        itoa(fd, buf, 10);
        kprint(buf);
        return;
    }
    
    char read_buf[128] = {0};
    bytes = sys_read(fd, read_buf, sizeof(read_buf) - 1);
    if (bytes < 0) {
        vga_puts("FAILED: Read error=");
        itoa(bytes, buf, 10);
        kprint(buf);
        sys_close(fd);
        return;
    }
    
    vga_puts("SUCCESS: Read ");
    itoa(bytes, buf, 10);
    vga_puts(buf);
    kprint(" bytes");
    vga_puts("Content: '");
    vga_puts(read_buf);
    kprint("'");
    
    sys_close(fd);
    
    if (strcmp(read_buf, test_data) == 0) {
        kprint("=== DISK WRITE TEST PASSED ===");
    } else {
        kprint("=== DISK WRITE TEST FAILED: Data mismatch ===");
    }
}

static void cmd_edit(const char* args) {
    if (!args || !*args) {
        kprint("Usage: edit <filename>");
        kprint("Opens text editor for file editing");
        return;
    }
    
    editor_context_t editor;
    editor_init(&editor);
    
    int ret = editor_open_file(&editor, args);
    if (ret < 0) {
        editor_new_file(&editor, args);
    }
    
    editor_run(&editor);
    editor_cleanup(&editor);
    
    vga_set_color(0x0F);
}

void cmd_module_filesystem_register(void) {
    command_register_with_category("test-fs", "", "Test VFS and ramfs operations", "Filesystem", cmd_test_fs);
    command_register_with_category("lst", "[path]", "List directory contents", "Filesystem", cmd_lst);
    command_register_with_category("view", "<filename>", "Display file contents", "Filesystem", cmd_view);
    command_register_with_category("edit", "<filename>", "Edit file in text editor", "Filesystem", cmd_edit);
    command_register_with_category("create", "<filename> [--empty]", "Create new file", "Filesystem", cmd_create);
    command_register_with_category("write", "<filename> <content>", "Write content to file", "Filesystem", cmd_write);
    command_register_with_category("rm", "[--force] <file|directory>", "Remove file or directory", "Filesystem", cmd_rm);
    command_register_with_category("mkfld", "<dirname>", "Create directory", "Filesystem", cmd_mkfld);
    command_register_with_category("go", "<directory>", "Change working directory", "Filesystem", cmd_go);
    command_register_with_category("pwd", "", "Print working directory", "Filesystem", cmd_pwd);
    command_register_with_category("disk-info", "", "Display disk information", "Filesystem", cmd_disk_info);
    command_register_with_category("install", "[--force]", "Install aOS layout (ABL bootloader + simplefs data partition)", "Filesystem", cmd_install);
    command_register_with_category("format", "<simplefs|fat32>", "Format target disk/partition", "Filesystem", cmd_format);
    command_register_with_category("test-disk", "", "Test disk operations", "Filesystem", cmd_test_disk_write);
}
