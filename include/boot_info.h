/*
 * === AOS HEADER BEGIN ===
 * include/boot_info.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>
#include "multiboot.h"

typedef enum {
    BOOT_PROTOCOL_UNKNOWN = 0,
    BOOT_PROTOCOL_MULTIBOOT1 = 1,
    BOOT_PROTOCOL_MULTIBOOT2 = 2
} boot_protocol_t;

typedef struct {
    boot_protocol_t protocol;
    uint32_t boot_magic;
    uintptr_t raw_info_addr;
    uint32_t raw_info_size;
    uint32_t multiboot2_tag_count;
    uint32_t module_count;
    uint32_t mmap_entry_count;
    const multiboot_info_t* compat_mbi;
} boot_runtime_info_t;

void boot_info_init(uint32_t multiboot_magic, const void* raw_boot_info);
const multiboot_info_t* boot_info_get_multiboot(void);
const boot_runtime_info_t* boot_info_get_runtime(void);

void boot_info_print_serial(void);
void boot_info_print_console(void);
void print_boot_info(const multiboot_info_t *mbi);

#endif
