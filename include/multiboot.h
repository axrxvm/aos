/*
 * === AOS HEADER BEGIN ===
 * include/multiboot.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

// Multiboot header magic
#define MULTIBOOT_HEADER_MAGIC     0x1BADB002
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT2_HEADER_MAGIC     0xE85250D6
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

// Multiboot info flags
#define MULTIBOOT_INFO_MEMORY             0x00000001
#define MULTIBOOT_INFO_BOOTDEV            0x00000002
#define MULTIBOOT_INFO_CMDLINE            0x00000004
#define MULTIBOOT_INFO_MODS               0x00000008
#define MULTIBOOT_INFO_AOUT_SYMS          0x00000010
#define MULTIBOOT_INFO_ELF_SHDR           0x00000020
#define MULTIBOOT_INFO_MEM_MAP            0x00000040
#define MULTIBOOT_INFO_DRIVE_INFO         0x00000080
#define MULTIBOOT_INFO_CONFIG_TABLE       0x00000100
#define MULTIBOOT_INFO_BOOT_LOADER_NAME   0x00000200
#define MULTIBOOT_INFO_APM_TABLE          0x00000400
#define MULTIBOOT_INFO_VBE_INFO           0x00000800
#define MULTIBOOT_INFO_FRAMEBUFFER_INFO   0x00001000

// Multiboot2 information tags
#define MULTIBOOT2_TAG_TYPE_END              0
#define MULTIBOOT2_TAG_TYPE_CMDLINE          1
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT2_TAG_TYPE_MODULE           3
#define MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO    4
#define MULTIBOOT2_TAG_TYPE_BOOTDEV          5
#define MULTIBOOT2_TAG_TYPE_MMAP             6
#define MULTIBOOT2_TAG_TYPE_VBE              7
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER      8
#define MULTIBOOT2_TAG_TYPE_APM              10

// Multiboot2 memory map entry types
#define MULTIBOOT2_MMAP_TYPE_AVAILABLE           1
#define MULTIBOOT2_MMAP_TYPE_RESERVED            2
#define MULTIBOOT2_MMAP_TYPE_ACPI_RECLAIMABLE    3
#define MULTIBOOT2_MMAP_TYPE_NVS                 4
#define MULTIBOOT2_MMAP_TYPE_BADRAM              5

// VBE mode info structure
typedef struct {
    uint16_t attributes;
    uint8_t  window_a;
    uint8_t  window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  w_char;
    uint8_t  y_char;
    uint8_t  planes;
    uint8_t  bpp;
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  reserved0;
    uint8_t  red_mask;
    uint8_t  red_position;
    uint8_t  green_mask;
    uint8_t  green_position;
    uint8_t  blue_mask;
    uint8_t  blue_position;
    uint8_t  reserved_mask;
    uint8_t  reserved_position;
    uint8_t  direct_color_attributes;
    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t  reserved1[206];
} __attribute__((packed)) multiboot_vbe_mode_info_t;

// VBE controller info
typedef struct {
    uint8_t  signature[4];
    uint16_t version;
    uint32_t oem_string;
    uint32_t capabilities;
    uint32_t video_modes;
    uint16_t total_memory;
    uint16_t oem_software_rev;
    uint32_t oem_vendor_name;
    uint32_t oem_product_name;
    uint32_t oem_product_rev;
    uint8_t  reserved[222];
    uint8_t  oem_data[256];
} __attribute__((packed)) multiboot_vbe_controller_info_t;

// Framebuffer color info
typedef struct {
    uint32_t framebuffer_palette_addr;
    uint16_t framebuffer_palette_num_colors;
} __attribute__((packed)) multiboot_color_indexed_t;

typedef struct {
    uint8_t framebuffer_red_field_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;
    uint8_t framebuffer_blue_mask_size;
} __attribute__((packed)) multiboot_color_rgb_t;

// Complete Multiboot info structure
typedef struct multiboot_info {
    // Multiboot info version 1.0
    uint32_t flags;                    // Offset 0: required flags
    
    // Available memory from BIOS (flags[0])
    uint32_t mem_lower;                // Offset 4: KB of lower memory
    uint32_t mem_upper;                // Offset 8: KB of upper memory
    
    // Boot device (flags[1])
    uint32_t boot_device;              // Offset 12: boot device
    
    // Kernel command line (flags[2])
    uint32_t cmdline;                  // Offset 16: command line pointer
    
    // Boot modules (flags[3])
    uint32_t mods_count;               // Offset 20: number of modules
    uint32_t mods_addr;                // Offset 24: module list address
    
    // Symbol table (flags[4] or flags[5])
    uint32_t syms[4];                  // Offset 28-43: symbol table info
    
    // Memory map (flags[6])
    uint32_t mmap_length;              // Offset 44: memory map length
    uint32_t mmap_addr;                // Offset 48: memory map address
    
    // Drives (flags[7])
    uint32_t drives_length;            // Offset 52: drives length
    uint32_t drives_addr;              // Offset 56: drives address
    
    // ROM configuration table (flags[8])
    uint32_t config_table;             // Offset 60: config table
    
    // Bootloader name (flags[9])
    uint32_t boot_loader_name;         // Offset 64: bootloader name string
    
    // APM table (flags[10])
    uint32_t apm_table;                // Offset 68: APM table
    
    // VBE video info (flags[11])
    uint32_t vbe_control_info;         // Offset 72: VBE controller info
    uint32_t vbe_mode_info;            // Offset 76: VBE mode info
    uint16_t vbe_mode;                 // Offset 80: current VBE mode
    uint16_t vbe_interface_seg;        // Offset 82: VBE interface segment
    uint16_t vbe_interface_off;        // Offset 84: VBE interface offset
    uint16_t vbe_interface_len;        // Offset 86: VBE interface length
    
    // Framebuffer info (flags[12])
    uint64_t framebuffer_addr;         // Offset 88: framebuffer address
    uint32_t framebuffer_pitch;        // Offset 96: framebuffer pitch
    uint32_t framebuffer_width;        // Offset 100: framebuffer width
    uint32_t framebuffer_height;       // Offset 104: framebuffer height
    uint8_t  framebuffer_bpp;          // Offset 108: bits per pixel
    uint8_t  framebuffer_type;         // Offset 109: framebuffer type
    union {
        multiboot_color_indexed_t indexed;
        multiboot_color_rgb_t rgb;
        uint8_t color_info[6];
    };                                  // Offset 110-115: color info
} __attribute__((packed)) multiboot_info_t;

// Memory map entry
typedef struct {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_memory_map_t;

// Multiboot2 information header
typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed)) multiboot2_info_t;

// Multiboot2 generic tag header
typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) multiboot2_tag_t;

// Multiboot2 null-terminated string tags
typedef struct {
    uint32_t type;
    uint32_t size;
    char string[0];
} __attribute__((packed)) multiboot2_tag_string_t;

// Multiboot2 module tag
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[0];
} __attribute__((packed)) multiboot2_tag_module_t;

// Multiboot2 basic memory info tag
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed)) multiboot2_tag_basic_meminfo_t;

// Multiboot2 boot device tag
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t biosdev;
    uint32_t slice;
    uint32_t part;
} __attribute__((packed)) multiboot2_tag_bootdev_t;

// Multiboot2 memory map tag header
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed)) multiboot2_tag_mmap_t;

// Multiboot2 memory map entry
typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) multiboot2_mmap_entry_t;

// Multiboot2 VBE tag
typedef struct {
    uint32_t type;
    uint32_t size;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint8_t vbe_control_info[512];
    uint8_t vbe_mode_info[256];
} __attribute__((packed)) multiboot2_tag_vbe_t;

// Multiboot2 framebuffer common header
typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} __attribute__((packed)) multiboot2_tag_framebuffer_common_t;

// Multiboot2 indexed framebuffer tail
typedef struct {
    uint16_t framebuffer_palette_num_colors;
    struct {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
    } __attribute__((packed)) framebuffer_palette[0];
} __attribute__((packed)) multiboot2_tag_framebuffer_indexed_t;

// Multiboot2 RGB framebuffer tail
typedef struct {
    uint8_t framebuffer_red_field_position;
    uint8_t framebuffer_red_mask_size;
    uint8_t framebuffer_green_field_position;
    uint8_t framebuffer_green_mask_size;
    uint8_t framebuffer_blue_field_position;
    uint8_t framebuffer_blue_mask_size;
} __attribute__((packed)) multiboot2_tag_framebuffer_rgb_t;

// Module structure
typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} __attribute__((packed)) multiboot_module_t;

// Framebuffer types
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED  0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB      1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2

#endif
