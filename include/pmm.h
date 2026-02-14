/*
 * === AOS HEADER BEGIN ===
 * include/pmm.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

// Memory zones for different allocation purposes
typedef enum {
    PMM_ZONE_DMA,       // 0-16MB: DMA-capable memory
    PMM_ZONE_NORMAL,    // 16MB-896MB: Normal allocations
    PMM_ZONE_HIGH,      // 896MB+: High memory (future expansion)
    PMM_ZONE_COUNT
} pmm_zone_t;

// Memory region descriptor
typedef struct pmm_region {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t type;           // Multiboot memory type
    struct pmm_region *next;
} pmm_region_t;

// Zone statistics
typedef struct {
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t reserved_frames;
    uint32_t start_frame;
    uint32_t end_frame;
} pmm_zone_stats_t;

// Initialize PMM with multiboot memory map
void init_pmm(uint32_t mem_size);
void init_pmm_advanced(uint32_t mem_size, void *mmap_addr, uint32_t mmap_length);

// Page allocation/deallocation
void* alloc_page(void);
void* alloc_page_from_zone(pmm_zone_t zone);
void* alloc_pages_contiguous(size_t num_pages);
void free_page(void* page);

// Validation and safety
int pmm_is_valid_frame(uint32_t frame_addr);
int pmm_is_frame_used(uint32_t frame_addr);

// Memory region management
int pmm_add_region(uint32_t start, uint32_t end, uint32_t type);
void pmm_reserve_region(uint32_t start, uint32_t end);

// Statistics functions
uint32_t pmm_get_total_frames(void);
uint32_t pmm_get_used_frames(void);
uint32_t pmm_get_free_frames(void);
void pmm_get_zone_stats(pmm_zone_t zone, pmm_zone_stats_t *stats);
void pmm_print_memory_map(void);
void pmm_print_detailed_stats(void);

// Debugging
int pmm_validate_integrity(void);
void pmm_dump_allocations(void);

#endif
