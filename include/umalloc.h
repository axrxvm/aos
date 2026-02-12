/*
 * === AOS HEADER BEGIN ===
 * include/umalloc.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef UMALLOC_H
#define UMALLOC_H

#include <stdint.h>
#include <stddef.h>

// Userspace memory allocator (aOS-style)
// Uses pool-based allocation for efficiency

#define UMEM_POOL_SIZE 8192  // 8KB per pool

// Memory block header
typedef struct mem_block {
    uint32_t size;
    int is_free;
    struct mem_block* next;
} mem_block_t;

// Initialize userspace memory allocator
void umem_init(void);

// Allocate memory in userspace
void* umalloc(size_t size);

// Free userspace memory
void ufree(void* ptr);

// Reallocate memory
void* urealloc(void* ptr, size_t new_size);

// Get allocation statistics
void umem_stats(uint32_t* total, uint32_t* used, uint32_t* free_blocks);

#endif // UMALLOC_H
