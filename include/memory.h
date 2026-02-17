/*
 * === AOS HEADER BEGIN ===
 * include/memory.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef MEMORY_H
#define MEMORY_H

#include "multiboot.h"
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define MAX_FRAMES (1024 * 1024 * 32 / PAGE_SIZE)

// PMM functions (declared in pmm.h, but kept for compatibility)
void* alloc_page(void);
void free_page(void* page);
void init_pmm(uint32_t mem_size);

// Memory information
void print_memory_info(const multiboot_info_t* mbi);

#endif
