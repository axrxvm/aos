/*
 * === AOS HEADER BEGIN ===
 * include/memory.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/*
 * DEVELOPER_NOTE_BLOCK
 * Module Overview:
 * - This file is part of the aOS production kernel/userspace codebase.
 * - Review public symbols in this unit to understand contracts with adjacent modules.
 * - Keep behavior-focused comments near non-obvious invariants, state transitions, and safety checks.
 * - Avoid changing ABI/data-layout assumptions without updating dependent modules.
 */


#ifndef MEMORY_H
#define MEMORY_H

#include "multiboot.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
/* Maximum frame count representable by 32-bit physical addresses. */
#define MAX_FRAMES (((uint32_t)(UINT32_MAX / PAGE_SIZE)) + 1U)

// PMM functions (declared in pmm.h, but kept for compatibility)
void* alloc_page(void);
void free_page(void* page);
void init_pmm(uint32_t mem_size);

// Memory information
void print_memory_info(const multiboot_info_t* mbi);

#endif
