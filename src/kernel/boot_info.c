/*
 * === AOS HEADER BEGIN ===
 * src/kernel/boot_info.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <stddef.h>
#include <serial.h>
#include <multiboot.h>
#include <boot_info.h>
#include <string.h>

extern void kprint(const char *str);

#define BOOTINFO_MAX_MODULES      32
#define BOOTINFO_MAX_MMAP_ENTRIES 128

static boot_runtime_info_t g_boot_runtime;
static multiboot_info_t g_compat_mbi;
static multiboot_module_t g_module_entries[BOOTINFO_MAX_MODULES];
static multiboot_memory_map_t g_mmap_entries[BOOTINFO_MAX_MMAP_ENTRIES];
static multiboot_vbe_controller_info_t g_vbe_controller_info;
static multiboot_vbe_mode_info_t g_vbe_mode_info;

static uint32_t align_up_8(uint32_t value) {
    return (value + 7U) & ~7U;
}

static uint32_t ptr32(const void* ptr) {
    return (uint32_t)(uintptr_t)ptr;
}

static void format_hex64(uint64_t value, char* out, size_t out_len) {
    static const char* hex = "0123456789ABCDEF";
    if (!out || out_len < 19) {
        return;
    }

    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        uint8_t nibble = (uint8_t)((value >> ((15 - i) * 4)) & 0xF);
        out[2 + i] = hex[nibble];
    }
    out[18] = '\0';
}

static void reset_boot_runtime(void) {
    memset(&g_boot_runtime, 0, sizeof(g_boot_runtime));
    memset(&g_compat_mbi, 0, sizeof(g_compat_mbi));
    memset(g_module_entries, 0, sizeof(g_module_entries));
    memset(g_mmap_entries, 0, sizeof(g_mmap_entries));
    memset(&g_vbe_controller_info, 0, sizeof(g_vbe_controller_info));
    memset(&g_vbe_mode_info, 0, sizeof(g_vbe_mode_info));
    g_boot_runtime.protocol = BOOT_PROTOCOL_UNKNOWN;
}

static uint32_t count_multiboot1_mmap_entries(const multiboot_info_t* mbi) {
    if (!mbi) {
        return 0;
    }

    if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP) || mbi->mmap_addr == 0 || mbi->mmap_length == 0) {
        return 0;
    }

    uintptr_t start_addr = (uintptr_t)mbi->mmap_addr;
    uintptr_t end_addr = start_addr + mbi->mmap_length;
    if (end_addr < start_addr) {
        return 0;
    }

    uint8_t* cursor = (uint8_t*)start_addr;
    uint8_t* end = (uint8_t*)end_addr;
    uint32_t count = 0;

    while (cursor + sizeof(uint32_t) <= end) {
        multiboot_memory_map_t* entry = (multiboot_memory_map_t*)cursor;
        uint32_t entry_size = entry->size + sizeof(uint32_t);
        if (entry_size == 0 || cursor + entry_size > end) {
            break;
        }
        count++;
        cursor += entry_size;
    }

    return count;
}

static void parse_multiboot1(uint32_t magic, const multiboot_info_t* mbi) {
    g_boot_runtime.protocol = BOOT_PROTOCOL_MULTIBOOT1;
    g_boot_runtime.boot_magic = magic;
    g_boot_runtime.raw_info_addr = (uintptr_t)mbi;
    g_boot_runtime.raw_info_size = sizeof(multiboot_info_t);

    if (!mbi) {
        return;
    }

    memcpy(&g_compat_mbi, mbi, sizeof(multiboot_info_t));
    g_boot_runtime.compat_mbi = &g_compat_mbi;

    if (mbi->flags & MULTIBOOT_INFO_MODS) {
        g_boot_runtime.module_count = mbi->mods_count;
    }
    g_boot_runtime.mmap_entry_count = count_multiboot1_mmap_entries(mbi);
}

static void parse_multiboot2_mmap_tag(const multiboot2_tag_mmap_t* mmap_tag, const multiboot2_tag_t* tag) {
    if (!mmap_tag || !tag) {
        return;
    }
    if (tag->size < sizeof(multiboot2_tag_mmap_t) || mmap_tag->entry_size < sizeof(multiboot2_mmap_entry_t)) {
        return;
    }

    const uint8_t* cursor = (const uint8_t*)(mmap_tag + 1);
    const uint8_t* end = ((const uint8_t*)tag) + tag->size;
    uint32_t copied_entries = 0;

    while (cursor + mmap_tag->entry_size <= end && copied_entries < BOOTINFO_MAX_MMAP_ENTRIES) {
        const multiboot2_mmap_entry_t* src = (const multiboot2_mmap_entry_t*)cursor;
        multiboot_memory_map_t* dst = &g_mmap_entries[copied_entries];
        dst->size = sizeof(multiboot_memory_map_t) - sizeof(uint32_t);
        dst->addr = src->addr;
        dst->len = src->len;
        dst->type = src->type;
        copied_entries++;
        cursor += mmap_tag->entry_size;
    }

    if (copied_entries > 0) {
        g_compat_mbi.flags |= MULTIBOOT_INFO_MEM_MAP;
        g_compat_mbi.mmap_addr = ptr32(g_mmap_entries);
        g_compat_mbi.mmap_length = copied_entries * sizeof(multiboot_memory_map_t);
        g_boot_runtime.mmap_entry_count = copied_entries;
    }
}

static void parse_multiboot2_vbe_tag(const multiboot2_tag_vbe_t* vbe_tag, const multiboot2_tag_t* tag) {
    if (!vbe_tag || !tag || tag->size < sizeof(multiboot2_tag_vbe_t)) {
        return;
    }

    memcpy(&g_vbe_controller_info, vbe_tag->vbe_control_info, sizeof(g_vbe_controller_info));
    memcpy(&g_vbe_mode_info, vbe_tag->vbe_mode_info, sizeof(g_vbe_mode_info));

    g_compat_mbi.flags |= MULTIBOOT_INFO_VBE_INFO;
    g_compat_mbi.vbe_control_info = ptr32(&g_vbe_controller_info);
    g_compat_mbi.vbe_mode_info = ptr32(&g_vbe_mode_info);
    g_compat_mbi.vbe_mode = vbe_tag->vbe_mode;
    g_compat_mbi.vbe_interface_seg = vbe_tag->vbe_interface_seg;
    g_compat_mbi.vbe_interface_off = vbe_tag->vbe_interface_off;
    g_compat_mbi.vbe_interface_len = vbe_tag->vbe_interface_len;
}

static void parse_multiboot2_framebuffer_tag(const multiboot2_tag_framebuffer_common_t* fb_tag, const multiboot2_tag_t* tag) {
    if (!fb_tag || !tag || tag->size < sizeof(multiboot2_tag_framebuffer_common_t)) {
        return;
    }

    g_compat_mbi.flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
    g_compat_mbi.framebuffer_addr = fb_tag->framebuffer_addr;
    g_compat_mbi.framebuffer_pitch = fb_tag->framebuffer_pitch;
    g_compat_mbi.framebuffer_width = fb_tag->framebuffer_width;
    g_compat_mbi.framebuffer_height = fb_tag->framebuffer_height;
    g_compat_mbi.framebuffer_bpp = fb_tag->framebuffer_bpp;
    g_compat_mbi.framebuffer_type = fb_tag->framebuffer_type;

    if (fb_tag->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED) {
        if (tag->size >= sizeof(multiboot2_tag_framebuffer_common_t) + sizeof(uint16_t)) {
            const uint8_t* tail = (const uint8_t*)(fb_tag + 1);
            uint16_t palette_colors = *((const uint16_t*)tail);
            g_compat_mbi.indexed.framebuffer_palette_num_colors = palette_colors;
            g_compat_mbi.indexed.framebuffer_palette_addr = ptr32(tail + sizeof(uint16_t));
        }
    } else if (fb_tag->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        if (tag->size >= sizeof(multiboot2_tag_framebuffer_common_t) + sizeof(multiboot2_tag_framebuffer_rgb_t)) {
            const multiboot2_tag_framebuffer_rgb_t* rgb = (const multiboot2_tag_framebuffer_rgb_t*)(fb_tag + 1);
            g_compat_mbi.rgb.framebuffer_red_field_position = rgb->framebuffer_red_field_position;
            g_compat_mbi.rgb.framebuffer_red_mask_size = rgb->framebuffer_red_mask_size;
            g_compat_mbi.rgb.framebuffer_green_field_position = rgb->framebuffer_green_field_position;
            g_compat_mbi.rgb.framebuffer_green_mask_size = rgb->framebuffer_green_mask_size;
            g_compat_mbi.rgb.framebuffer_blue_field_position = rgb->framebuffer_blue_field_position;
            g_compat_mbi.rgb.framebuffer_blue_mask_size = rgb->framebuffer_blue_mask_size;
        }
    }
}

static void parse_multiboot2(uint32_t magic, const multiboot2_info_t* mb2) {
    g_boot_runtime.protocol = BOOT_PROTOCOL_MULTIBOOT2;
    g_boot_runtime.boot_magic = magic;
    g_boot_runtime.raw_info_addr = (uintptr_t)mb2;

    if (!mb2) {
        return;
    }
    if (mb2->total_size < sizeof(multiboot2_info_t)) {
        return;
    }

    uintptr_t start_addr = (uintptr_t)mb2;
    uintptr_t end_addr = start_addr + mb2->total_size;
    if (end_addr < start_addr) {
        return;
    }

    const uint8_t* start = (const uint8_t*)start_addr;
    const uint8_t* end = (const uint8_t*)end_addr;
    const uint8_t* cursor = start + sizeof(multiboot2_info_t);

    g_boot_runtime.raw_info_size = mb2->total_size;
    g_boot_runtime.compat_mbi = &g_compat_mbi;

    while (cursor + sizeof(multiboot2_tag_t) <= end) {
        const multiboot2_tag_t* tag = (const multiboot2_tag_t*)cursor;
        if (tag->size < sizeof(multiboot2_tag_t) || cursor + tag->size > end) {
            break;
        }

        if (tag->type == MULTIBOOT2_TAG_TYPE_END) {
            break;
        }
        g_boot_runtime.multiboot2_tag_count++;

        switch (tag->type) {
            case MULTIBOOT2_TAG_TYPE_CMDLINE: {
                const multiboot2_tag_string_t* cmdline_tag = (const multiboot2_tag_string_t*)tag;
                g_compat_mbi.flags |= MULTIBOOT_INFO_CMDLINE;
                g_compat_mbi.cmdline = ptr32(cmdline_tag->string);
                break;
            }
            case MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME: {
                const multiboot2_tag_string_t* bootloader_tag = (const multiboot2_tag_string_t*)tag;
                g_compat_mbi.flags |= MULTIBOOT_INFO_BOOT_LOADER_NAME;
                g_compat_mbi.boot_loader_name = ptr32(bootloader_tag->string);
                break;
            }
            case MULTIBOOT2_TAG_TYPE_MODULE: {
                const multiboot2_tag_module_t* module_tag = (const multiboot2_tag_module_t*)tag;
                if (g_boot_runtime.module_count < BOOTINFO_MAX_MODULES) {
                    multiboot_module_t* dst = &g_module_entries[g_boot_runtime.module_count];
                    dst->mod_start = module_tag->mod_start;
                    dst->mod_end = module_tag->mod_end;
                    dst->cmdline = ptr32(module_tag->cmdline);
                    dst->pad = 0;
                    g_boot_runtime.module_count++;
                }
                break;
            }
            case MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO: {
                const multiboot2_tag_basic_meminfo_t* mem_tag = (const multiboot2_tag_basic_meminfo_t*)tag;
                g_compat_mbi.flags |= MULTIBOOT_INFO_MEMORY;
                g_compat_mbi.mem_lower = mem_tag->mem_lower;
                g_compat_mbi.mem_upper = mem_tag->mem_upper;
                break;
            }
            case MULTIBOOT2_TAG_TYPE_BOOTDEV: {
                const multiboot2_tag_bootdev_t* bootdev_tag = (const multiboot2_tag_bootdev_t*)tag;
                uint32_t biosdev = (bootdev_tag->biosdev == 0xFFFFFFFFU) ? 0xFFU : (bootdev_tag->biosdev & 0xFFU);
                uint32_t slice = (bootdev_tag->slice == 0xFFFFFFFFU) ? 0xFFU : (bootdev_tag->slice & 0xFFU);
                uint32_t part = (bootdev_tag->part == 0xFFFFFFFFU) ? 0xFFU : (bootdev_tag->part & 0xFFU);
                g_compat_mbi.flags |= MULTIBOOT_INFO_BOOTDEV;
                g_compat_mbi.boot_device = biosdev | (slice << 8) | (part << 16) | (0xFFU << 24);
                break;
            }
            case MULTIBOOT2_TAG_TYPE_MMAP:
                parse_multiboot2_mmap_tag((const multiboot2_tag_mmap_t*)tag, tag);
                break;
            case MULTIBOOT2_TAG_TYPE_VBE:
                parse_multiboot2_vbe_tag((const multiboot2_tag_vbe_t*)tag, tag);
                break;
            case MULTIBOOT2_TAG_TYPE_FRAMEBUFFER:
                parse_multiboot2_framebuffer_tag((const multiboot2_tag_framebuffer_common_t*)tag, tag);
                break;
            default:
                break;
        }

        uint32_t step = align_up_8(tag->size);
        if (step < tag->size) {
            break;
        }
        cursor += step;
    }

    if (g_boot_runtime.module_count > 0) {
        g_compat_mbi.flags |= MULTIBOOT_INFO_MODS;
        g_compat_mbi.mods_count = g_boot_runtime.module_count;
        g_compat_mbi.mods_addr = ptr32(g_module_entries);
    }
}

void boot_info_init(uint32_t multiboot_magic, const void* raw_boot_info) {
    reset_boot_runtime();

    if (multiboot_magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        parse_multiboot1(multiboot_magic, (const multiboot_info_t*)raw_boot_info);
        return;
    }
    if (multiboot_magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        parse_multiboot2(multiboot_magic, (const multiboot2_info_t*)raw_boot_info);
        return;
    }

    g_boot_runtime.protocol = BOOT_PROTOCOL_UNKNOWN;
    g_boot_runtime.boot_magic = multiboot_magic;
    g_boot_runtime.raw_info_addr = (uintptr_t)raw_boot_info;
}

const multiboot_info_t* boot_info_get_multiboot(void) {
    return g_boot_runtime.compat_mbi;
}

const boot_runtime_info_t* boot_info_get_runtime(void) {
    return &g_boot_runtime;
}

void boot_info_print_serial(void) {
    serial_puts("\n=== Boot Runtime Information ===\n");
    serial_puts("Protocol: ");
    if (g_boot_runtime.protocol == BOOT_PROTOCOL_MULTIBOOT1) {
        serial_puts("Multiboot1\n");
    } else if (g_boot_runtime.protocol == BOOT_PROTOCOL_MULTIBOOT2) {
        serial_puts("Multiboot2\n");
    } else {
        serial_puts("Unknown\n");
    }

    serial_puts("Boot magic: 0x");
    serial_put_uint32(g_boot_runtime.boot_magic);
    serial_puts("\nRaw info addr: 0x");
    serial_put_uint32((uint32_t)g_boot_runtime.raw_info_addr);
    serial_puts("\nRaw info size: 0x");
    serial_put_uint32(g_boot_runtime.raw_info_size);
    serial_puts("\nMB2 tag count: ");
    serial_put_uint32(g_boot_runtime.multiboot2_tag_count);
    serial_puts("\n");

    if (g_boot_runtime.compat_mbi) {
        print_boot_info(g_boot_runtime.compat_mbi);
    } else {
        serial_puts("No parsed compatible multiboot structure available.\n");
    }
}

void boot_info_print_console(void) {
    char line[128];
    const multiboot_info_t* mbi = g_boot_runtime.compat_mbi;

    kprint("=== Boot Information ===");

    if (g_boot_runtime.protocol == BOOT_PROTOCOL_MULTIBOOT1) {
        kprint("Protocol: Multiboot1");
    } else if (g_boot_runtime.protocol == BOOT_PROTOCOL_MULTIBOOT2) {
        kprint("Protocol: Multiboot2");
    } else {
        kprint("Protocol: Unknown");
    }

    snprintf(line, sizeof(line), "Boot magic: 0x%x", g_boot_runtime.boot_magic);
    kprint(line);
    snprintf(line, sizeof(line), "Raw info addr: 0x%x", (uint32_t)g_boot_runtime.raw_info_addr);
    kprint(line);
    snprintf(line, sizeof(line), "Raw info size: %d bytes", (int)g_boot_runtime.raw_info_size);
    kprint(line);
    if (g_boot_runtime.protocol == BOOT_PROTOCOL_MULTIBOOT2) {
        snprintf(line, sizeof(line), "Multiboot2 tags parsed: %d", (int)g_boot_runtime.multiboot2_tag_count);
        kprint(line);
    }

    if (!mbi) {
        kprint("No compatible boot data available.");
        kprint("========================");
        return;
    }

    snprintf(line, sizeof(line), "Flags: 0x%x", mbi->flags);
    kprint(line);

    if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        snprintf(line, sizeof(line), "Memory: lower=%dKB upper=%dKB", (int)mbi->mem_lower, (int)mbi->mem_upper);
        kprint(line);
    }

    if ((mbi->flags & MULTIBOOT_INFO_CMDLINE) && mbi->cmdline) {
        snprintf(line, sizeof(line), "Cmdline: %s", (char*)(uintptr_t)mbi->cmdline);
        kprint(line);
    }

    if ((mbi->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME) && mbi->boot_loader_name) {
        snprintf(line, sizeof(line), "Bootloader: %s", (char*)(uintptr_t)mbi->boot_loader_name);
        kprint(line);
    }

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        snprintf(line, sizeof(line), "Memory map entries: %d", (int)g_boot_runtime.mmap_entry_count);
        kprint(line);

        if (mbi->mmap_addr && mbi->mmap_length) {
            uintptr_t mmap_start = (uintptr_t)mbi->mmap_addr;
            uintptr_t mmap_end = mmap_start + mbi->mmap_length;
            if (mmap_end >= mmap_start) {
                const uint8_t* cursor = (const uint8_t*)mmap_start;
                const uint8_t* end = (const uint8_t*)mmap_end;
                uint32_t entry_index = 0;
                uint32_t shown = 0;

                while (cursor + sizeof(uint32_t) <= end && shown < 8) {
                    const multiboot_memory_map_t* entry = (const multiboot_memory_map_t*)cursor;
                    uint32_t entry_size = entry->size + sizeof(uint32_t);
                    if (entry_size == 0 || cursor + entry_size > end) {
                        break;
                    }

                    snprintf(line, sizeof(line), "  mmap[%d]: base=0x%x len=0x%x type=%d",
                             (int)entry_index, (uint32_t)entry->addr, (uint32_t)entry->len, (int)entry->type);
                    kprint(line);

                    cursor += entry_size;
                    entry_index++;
                    shown++;
                }
            }
        }
    }

    if (mbi->flags & MULTIBOOT_INFO_MODS) {
        snprintf(line, sizeof(line), "Modules: %d", (int)mbi->mods_count);
        kprint(line);

        if (mbi->mods_addr && mbi->mods_count > 0) {
            const multiboot_module_t* mods = (const multiboot_module_t*)(uintptr_t)mbi->mods_addr;
            uint32_t shown = (mbi->mods_count < 8) ? mbi->mods_count : 8;
            for (uint32_t i = 0; i < shown; i++) {
                const char* module_cmdline = mods[i].cmdline ? (const char*)(uintptr_t)mods[i].cmdline : "";
                snprintf(line, sizeof(line), "  mod[%d]: 0x%x-0x%x %s", (int)i,
                         mods[i].mod_start, mods[i].mod_end, module_cmdline);
                kprint(line);
            }
        }
    }

    if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        char fb_addr[20];
        format_hex64(mbi->framebuffer_addr, fb_addr, sizeof(fb_addr));
        snprintf(line, sizeof(line), "Framebuffer: %s %dx%dx%d", fb_addr,
                 (int)mbi->framebuffer_width, (int)mbi->framebuffer_height, (int)mbi->framebuffer_bpp);
        kprint(line);
    }

    kprint("========================");
}

void print_boot_info(const multiboot_info_t *mbi) {
    if (!mbi) {
        serial_puts("Multiboot information unavailable.\n");
        return;
    }

    serial_puts("\n=== Multiboot Information ===\n");
    serial_puts("Flags: 0x");
    serial_put_uint32(mbi->flags);
    serial_puts("\n");

    if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        serial_puts("Memory lower (KB): 0x");
        serial_put_uint32(mbi->mem_lower);
        serial_puts("\nMemory upper (KB): 0x");
        serial_put_uint32(mbi->mem_upper);
        serial_puts("\n");
    }

    if (mbi->flags & MULTIBOOT_INFO_BOOTDEV) {
        serial_puts("Boot device: 0x");
        serial_put_uint32(mbi->boot_device);
        serial_puts("\n");
    }

    if ((mbi->flags & MULTIBOOT_INFO_CMDLINE) && mbi->cmdline) {
        serial_puts("Command line: ");
        serial_puts((char*)(uintptr_t)mbi->cmdline);
        serial_puts("\n");
    }

    if ((mbi->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME) && mbi->boot_loader_name) {
        serial_puts("Bootloader: ");
        serial_puts((char*)(uintptr_t)mbi->boot_loader_name);
        serial_puts("\n");
    }

    if (mbi->flags & MULTIBOOT_INFO_MODS) {
        serial_puts("Modules: 0x");
        serial_put_uint32(mbi->mods_count);
        serial_puts(" @ 0x");
        serial_put_uint32(mbi->mods_addr);
        serial_puts("\n");
    }

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        serial_puts("MMap length: 0x");
        serial_put_uint32(mbi->mmap_length);
        serial_puts(" @ 0x");
        serial_put_uint32(mbi->mmap_addr);
        serial_puts("\n");
    }

    if (mbi->flags & MULTIBOOT_INFO_VBE_INFO) {
        serial_puts("VBE mode: 0x");
        serial_put_uint32(mbi->vbe_mode);
        serial_puts("\n");
    }

    if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        serial_puts("Framebuffer pitch: 0x");
        serial_put_uint32(mbi->framebuffer_pitch);
        serial_puts("\nFramebuffer width: 0x");
        serial_put_uint32(mbi->framebuffer_width);
        serial_puts("\nFramebuffer height: 0x");
        serial_put_uint32(mbi->framebuffer_height);
        serial_puts("\nFramebuffer bpp: 0x");
        serial_put_uint32(mbi->framebuffer_bpp);
        serial_puts("\n");
    }

    serial_puts("=== End Multiboot Information ===\n");
}

char hex_digit(uint8_t val) {
    return (val < 10) ? (char)('0' + val) : (char)('A' + (val - 10));
}

void serial_put_uint32(uint32_t n) {
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex_digit((uint8_t)((n >> i) & 0xF)));
    }
}
