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

// Forward declaration
extern void kprint(const char *str);

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
    uint32_t num_blocks = total_sectors;
    
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
    
    // Display warning and format information
    kprint("=== WARNING: FORMAT DISK ===");
    kprint("");
    kprint("This will ERASE ALL DATA on the disk!");
    kprint("");
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
        itoa(num_blocks, temp, 10);
        strcat(buf, temp);
        kprint(buf);
    } else if (strcmp(fstype, "fat32") == 0) {
        strcpy(buf, "Sectors:        ");
        itoa(total_sectors, temp, 10);
        strcat(buf, temp);
        kprint(buf);
        
        strcpy(buf, "Volume Label:   aOS_DISK");
        kprint(buf);
    }
    
    kprint("");
    kprint("Formatting...");
    
    // Perform format
    if (strcmp(fstype, "simplefs") == 0) {
        result = simplefs_format(0, num_blocks);
    } else if (strcmp(fstype, "fat32") == 0) {
        result = fat32_format(0, total_sectors, "aOS_DISK");
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
    command_register_with_category("format", "", "Format disk with SimpleFS", "Filesystem", cmd_format);
    command_register_with_category("test-disk", "", "Test disk operations", "Filesystem", cmd_test_disk_write);
}
