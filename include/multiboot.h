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

#define MULTIBOOT_INFO_MEM_MAP     0x00000040

typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t mmap_addr;
    uint32_t mmap_length;
    // ... other fields you might need later
} multiboot_info_t;

#endif
