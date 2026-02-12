/*
 * === AOS HEADER BEGIN ===
 * include/memory.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include "multiboot.h"
#ifndef MEMORY_H
#define MEMORY_H

#define PAGE_SIZE 4096
#define MAX_FRAMES (1024 * 1024 * 32 / PAGE_SIZE)
void* alloc_page(void);
void free_page(void* page);
void set_frame(uint32_t frame_index); 
void clear_frame(uint32_t frame_index); 
void init_pmm(uint32_t mem_size);
void print_memory_info(multiboot_info_t* mbi);


#endif
