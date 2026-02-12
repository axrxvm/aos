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

#include "multiboot.h"

void print_boot_info(const multiboot_info_t *mbi);

#endif
