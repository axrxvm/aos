/*
 * === AOS HEADER BEGIN ===
 * include/abl_boot.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ABL_BOOT_H
#define ABL_BOOT_H

#include <stdint.h>

// "ABL1" in little-endian
#define ABL_BOOT_MAGIC 0x314C4241U
#define ABL_INFO_VERSION 1U

#define ABL_INFO_FLAG_BOOT_DRIVE     0x00000001U
#define ABL_INFO_FLAG_KERNEL_PAYLOAD 0x00000002U
#define ABL_INFO_FLAG_MEMORY_INFO    0x00000004U
#define ABL_INFO_FLAG_MEMORY_MAP     0x00000008U
#define ABL_INFO_FLAG_STAGE2_LAYOUT  0x00000010U
#define ABL_INFO_FLAG_CMDLINE        0x00000020U
#define ABL_INFO_FLAG_MODULES        0x00000040U
#define ABL_INFO_FLAG_VBE_INFO       0x00000080U
#define ABL_INFO_FLAG_FRAMEBUFFER    0x00000100U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t boot_drive;
    uint32_t stage2_lba;
    uint32_t stage2_sectors;
    uint32_t kernel_lba;
    uint32_t kernel_sectors;
    uint32_t mem_lower_kb;
    uint32_t mem_upper_kb;
    uint32_t mmap_addr;
    uint32_t mmap_length;
    uint32_t mmap_entry_size;
    uint32_t mmap_entry_count;
    uint32_t cmdline_addr;
    uint32_t modules_addr;
    uint32_t modules_count;
    uint32_t vbe_control_info_addr;
    uint32_t vbe_mode_info_addr;
    uint32_t vbe_mode;
    uint32_t vbe_interface_seg;
    uint32_t vbe_interface_off;
    uint32_t vbe_interface_len;
    uint32_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_bpp;
    uint32_t framebuffer_type;
} abl_boot_info_t;

#endif // ABL_BOOT_H
