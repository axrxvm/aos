/*
 * === AOS HEADER BEGIN ===
 * ./include/pmm.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef PMM_H
#define PMM_H

#include <stdint.h>

void init_pmm(uint32_t mem_size);
void* alloc_page(void);
void free_page(void* page);

// Statistics functions
uint32_t pmm_get_total_frames(void);
uint32_t pmm_get_used_frames(void);
uint32_t pmm_get_free_frames(void);

#endif
