/*
 * === AOS HEADER BEGIN ===
 * src/mm/pmm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <serial.h> // Include for serial_puts
#include <panic.h>
#include <memory.h>
#include <pmm.h>
#include <stdlib.h> // For itoa
#include <string.h>

// Memory zone boundaries (in frame numbers)
#define DMA_ZONE_END        (16 * 1024 * 1024 / PAGE_SIZE)      // 16MB
#define NORMAL_ZONE_END     (896 * 1024 * 1024 / PAGE_SIZE)     // 896MB
#define KERNEL_RESERVED     512  // First 2MB reserved for kernel

// Frame bitmap and tracking
static uint32_t frame_bitmap[MAX_FRAMES / 32];
static uint32_t total_frames = 0;
static uint32_t used_frames = 0;

// Memory zones
static pmm_zone_stats_t zones[PMM_ZONE_COUNT];

// Stack-based allocator for fast single page allocation
#define FRAME_STACK_SIZE 256
static uint32_t frame_stack[FRAME_STACK_SIZE];
static int frame_stack_top = -1;

// Memory region list
static pmm_region_t *region_list = NULL;
static pmm_region_t region_pool[32];  // Pre-allocated region descriptors
static int region_pool_index = 0;

// Allocation statistics
static uint32_t alloc_count = 0;
static uint32_t free_count = 0;
static uint32_t failed_alloc_count = 0;

// Internal helper functions
static inline void set_frame(uint32_t frame_index) {
    if (frame_index >= MAX_FRAMES) {
        serial_puts("CRITICAL: set_frame - frame_index out of bounds\n");
        return;
    }
    uint32_t index = frame_index / 32;
    uint32_t offset = frame_index % 32;
    uint32_t old_val = frame_bitmap[index];
    frame_bitmap[index] |= (1 << offset);
    
    // Track if this was actually a new allocation
    if (!(old_val & (1 << offset))) {
        used_frames++;
    }
}

static inline void clear_frame(uint32_t frame_index) {
    if (frame_index >= MAX_FRAMES) {
        serial_puts("CRITICAL: clear_frame - frame_index out of bounds\n");
        return;
    }
    uint32_t index = frame_index / 32;
    uint32_t offset = frame_index % 32;
    uint32_t old_val = frame_bitmap[index];
    frame_bitmap[index] &= ~(1 << offset);
    
    // Track if this was actually a deallocation
    if (old_val & (1 << offset)) {
        used_frames--;
    }
}

static inline uint8_t test_frame(uint32_t frame_index) {
    if (frame_index >= MAX_FRAMES) {
        return 1; // Return as allocated to prevent allocation
    }
    uint32_t index = frame_index / 32;
    uint32_t offset = frame_index % 32;
    return (frame_bitmap[index] & (1 << offset)) != 0;
}

// Find free frame in specific zone
static int find_free_frame_in_zone(pmm_zone_t zone) {
    uint32_t start_frame = zones[zone].start_frame;
    uint32_t end_frame = zones[zone].end_frame;
    
    // Ensure we don't allocate reserved frames
    if (zone == PMM_ZONE_DMA && start_frame < KERNEL_RESERVED) {
        start_frame = KERNEL_RESERVED;
    }
    
    uint32_t start_bitmap = start_frame / 32;
    uint32_t end_bitmap = (end_frame + 31) / 32;
    
    for (uint32_t i = start_bitmap; i < end_bitmap; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (frame >= start_frame && frame < end_frame && 
                    frame >= KERNEL_RESERVED && frame < total_frames && 
                    !(frame_bitmap[i] & (1 << j))) {
                    return frame;
                }
            }
        }
    }
    return -1;
}

// Fast allocation from stack
static void* alloc_from_stack(void) {
    if (frame_stack_top >= 0) {
        uint32_t frame = frame_stack[frame_stack_top--];
        if (frame < KERNEL_RESERVED || frame >= total_frames) {
            serial_puts("WARNING: Invalid frame from stack\n");
            return alloc_page(); // Fallback to regular allocation
        }
        set_frame(frame);
        alloc_count++;
        return (void*)(frame * PAGE_SIZE);
    }
    return NULL;
}

// Add frame to stack for fast reallocation
static void add_to_stack(uint32_t frame) {
    if (frame_stack_top < FRAME_STACK_SIZE - 1) {
        frame_stack[++frame_stack_top] = frame;
    }
    // If stack is full, frame goes back to bitmap (slower allocation path)
}

// Initialize zone boundaries
static void init_zones(uint32_t mem_size) {
    uint32_t total = mem_size / PAGE_SIZE;
    
    // DMA Zone: 0-16MB
    zones[PMM_ZONE_DMA].start_frame = 0;
    zones[PMM_ZONE_DMA].end_frame = (total < DMA_ZONE_END) ? total : DMA_ZONE_END;
    zones[PMM_ZONE_DMA].total_frames = zones[PMM_ZONE_DMA].end_frame - zones[PMM_ZONE_DMA].start_frame;
    zones[PMM_ZONE_DMA].used_frames = 0;
    zones[PMM_ZONE_DMA].reserved_frames = KERNEL_RESERVED;
    
    // Normal Zone: 16MB-896MB
    if (total > DMA_ZONE_END) {
        zones[PMM_ZONE_NORMAL].start_frame = DMA_ZONE_END;
        zones[PMM_ZONE_NORMAL].end_frame = (total < NORMAL_ZONE_END) ? total : NORMAL_ZONE_END;
        zones[PMM_ZONE_NORMAL].total_frames = zones[PMM_ZONE_NORMAL].end_frame - zones[PMM_ZONE_NORMAL].start_frame;
        zones[PMM_ZONE_NORMAL].used_frames = 0;
        zones[PMM_ZONE_NORMAL].reserved_frames = 0;
    } else {
        zones[PMM_ZONE_NORMAL].start_frame = 0;
        zones[PMM_ZONE_NORMAL].end_frame = 0;
        zones[PMM_ZONE_NORMAL].total_frames = 0;
        zones[PMM_ZONE_NORMAL].used_frames = 0;
        zones[PMM_ZONE_NORMAL].reserved_frames = 0;
    }
    
    // High Zone: 896MB+ (future expansion)
    if (total > NORMAL_ZONE_END) {
        zones[PMM_ZONE_HIGH].start_frame = NORMAL_ZONE_END;
        zones[PMM_ZONE_HIGH].end_frame = total;
        zones[PMM_ZONE_HIGH].total_frames = zones[PMM_ZONE_HIGH].end_frame - zones[PMM_ZONE_HIGH].start_frame;
        zones[PMM_ZONE_HIGH].used_frames = 0;
        zones[PMM_ZONE_HIGH].reserved_frames = 0;
    } else {
        zones[PMM_ZONE_HIGH].start_frame = 0;
        zones[PMM_ZONE_HIGH].end_frame = 0;
        zones[PMM_ZONE_HIGH].total_frames = 0;
        zones[PMM_ZONE_HIGH].used_frames = 0;
        zones[PMM_ZONE_HIGH].reserved_frames = 0;
    }
}

void init_pmm(uint32_t mem_size) {
    // If no memory size provided, assume 32MB as minimum
    if (mem_size == 0) {
        mem_size = 32 * 1024 * 1024; // 32MB
        serial_puts("Warning: No memory size provided, assuming 32MB\n");
    }
    
    total_frames = mem_size / PAGE_SIZE;
    
    // Validate total_frames doesn't exceed MAX_FRAMES
    if (total_frames > MAX_FRAMES) {
        serial_puts("WARNING: Memory size exceeds MAX_FRAMES, capping\n");
        total_frames = MAX_FRAMES;
    }
    
    char buf[16];
    serial_puts("PMM: Total memory size: ");
    itoa(mem_size, buf, 10);
    serial_puts(buf);
    serial_puts(" bytes (");
    itoa(total_frames, buf, 10);
    serial_puts(buf);
    serial_puts(" frames)\n");

    // Clear all frames (set all bits to 0 = free)
    for (uint32_t i = 0; i < MAX_FRAMES / 32; i++) {
        frame_bitmap[i] = 0;
    }
    
    // Initialize zones
    init_zones(mem_size);
    
    // Initialize statistics
    used_frames = 0;
    alloc_count = 0;
    free_count = 0;
    failed_alloc_count = 0;
    
    // Reserve kernel frames (first 2MB)
    for (uint32_t i = 0; i < KERNEL_RESERVED; i++) {
        set_frame(i);
    }

    serial_puts("PMM initialized successfully with zone-based allocation.\n");
}

void init_pmm_advanced(uint32_t mem_size, void *mmap_addr, uint32_t mmap_length) {
    // First do basic initialization
    init_pmm(mem_size);
    
    // Then process memory map if provided
    // This would parse multiboot memory map to mark unusable regions
    // TODO: Implement multiboot memory map parsing
    serial_puts("PMM: Advanced initialization complete\n");
}

void* alloc_page() {
    // Try fast path first (stack allocator)
    void *page = alloc_from_stack();
    if (page) {
        return page;
    }
    
    // Try Normal zone first (most common allocation)
    int frame = find_free_frame_in_zone(PMM_ZONE_NORMAL);
    
    // Fallback to DMA zone if Normal zone is exhausted
    if (frame == -1 && zones[PMM_ZONE_DMA].total_frames > 0) {
        frame = find_free_frame_in_zone(PMM_ZONE_DMA);
    }
    
    // Last resort: try High zone
    if (frame == -1 && zones[PMM_ZONE_HIGH].total_frames > 0) {
        frame = find_free_frame_in_zone(PMM_ZONE_HIGH);
    }
    
    if (frame == -1) {
        failed_alloc_count++;
        serial_puts("CRITICAL: Out of physical memory! Allocations: ");
        char buf[16];
        itoa(alloc_count, buf, 10);
        serial_puts(buf);
        serial_puts(" Free: ");
        itoa(free_count, buf, 10);
        serial_puts(buf);
        serial_puts(" Failed: ");
        itoa(failed_alloc_count, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        panic("Out of physical memory!");
    }
    
    set_frame(frame);
    alloc_count++;
    return (void*)(frame * PAGE_SIZE);
}

void* alloc_page_from_zone(pmm_zone_t zone) {
    if (zone >= PMM_ZONE_COUNT) {
        serial_puts("ERROR: Invalid zone specified\n");
        return NULL;
    }
    
    int frame = find_free_frame_in_zone(zone);
    if (frame == -1) {
        failed_alloc_count++;
        return NULL;
    }
    
    set_frame(frame);
    alloc_count++;
    zones[zone].used_frames++;
    return (void*)(frame * PAGE_SIZE);
}

void* alloc_pages_contiguous(size_t num_pages) {
    if (num_pages == 0 || num_pages > 1024) {
        serial_puts("ERROR: Invalid contiguous allocation size\n");
        return NULL;
    }
    
    // Search for contiguous free frames
    for (uint32_t start = KERNEL_RESERVED; start <= total_frames - num_pages; start++) {
        int found = 1;
        
        // Check if all frames in range are free
        for (size_t i = 0; i < num_pages; i++) {
            if (test_frame(start + i)) {
                found = 0;
                start += i; // Skip ahead
                break;
            }
        }
        
        if (found) {
            // Allocate all frames
            for (size_t i = 0; i < num_pages; i++) {
                set_frame(start + i);
            }
            alloc_count += num_pages;
            return (void*)(start * PAGE_SIZE);
        }
    }
    
    failed_alloc_count++;
    serial_puts("WARNING: Could not find contiguous frames\n");
    return NULL;
}

void free_page(void* page) {
    if (!page) {
        serial_puts("WARNING: free_page called with NULL pointer\n");
        return;
    }
    
    uint32_t frame = (uint32_t)page / PAGE_SIZE;
    
    // Validate frame is within bounds
    if (frame >= total_frames || frame >= MAX_FRAMES) {
        serial_puts("ERROR: free_page - frame out of bounds: 0x");
        char buf[16];
        itoa(frame, buf, 16);
        serial_puts(buf);
        serial_puts("\n");
        return;
    }
    
    // Prevent freeing reserved kernel frames
    if (frame < KERNEL_RESERVED) {
        serial_puts("ERROR: free_page - attempt to free reserved frame: ");
        char buf[16];
        itoa(frame, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        return;
    }
    
    // Check for double-free
    if (!test_frame(frame)) {
        serial_puts("ERROR: Double-free detected! Frame: ");
        char buf[16];
        itoa(frame, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        return;
    }
    
    clear_frame(frame);
    add_to_stack(frame);
    free_count++;
}

int pmm_is_valid_frame(uint32_t frame_addr) {
    uint32_t frame = frame_addr / PAGE_SIZE;
    return (frame >= KERNEL_RESERVED && frame < total_frames);
}

int pmm_is_frame_used(uint32_t frame_addr) {
    uint32_t frame = frame_addr / PAGE_SIZE;
    if (frame >= total_frames) {
        return 0;
    }
    return test_frame(frame);
}

int pmm_add_region(uint32_t start, uint32_t end, uint32_t type) {
    if (region_pool_index >= 32) {
        serial_puts("ERROR: Region pool exhausted\n");
        return -1;
    }
    
    pmm_region_t *region = &region_pool[region_pool_index++];
    region->start_addr = start;
    region->end_addr = end;
    region->type = type;
    region->next = region_list;
    region_list = region;
    
    return 0;
}

void pmm_reserve_region(uint32_t start, uint32_t end) {
    uint32_t start_frame = start / PAGE_SIZE;
    uint32_t end_frame = (end + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (uint32_t frame = start_frame; frame < end_frame && frame < total_frames; frame++) {
        set_frame(frame);
    }
    
    char buf[16];
    serial_puts("PMM: Reserved region 0x");
    itoa(start, buf, 16);
    serial_puts(buf);
    serial_puts(" - 0x");
    itoa(end, buf, 16);
    serial_puts(buf);
    serial_puts("\n");
}

// Statistics functions
uint32_t pmm_get_total_frames(void) {
    return total_frames;
}

uint32_t pmm_get_used_frames(void) {
    return used_frames;
}

uint32_t pmm_get_free_frames(void) {
    return total_frames - used_frames;
}

void pmm_get_zone_stats(pmm_zone_t zone, pmm_zone_stats_t *stats) {
    if (zone >= PMM_ZONE_COUNT || !stats) {
        return;
    }
    
    stats->total_frames = zones[zone].total_frames;
    stats->used_frames = 0;
    stats->reserved_frames = zones[zone].reserved_frames;
    stats->start_frame = zones[zone].start_frame;
    stats->end_frame = zones[zone].end_frame;
    
    // Count actually used frames in this zone
    for (uint32_t frame = zones[zone].start_frame; frame < zones[zone].end_frame; frame++) {
        if (test_frame(frame)) {
            stats->used_frames++;
        }
    }
}

void pmm_print_memory_map(void) {
    serial_puts("\n=== Physical Memory Map ===\n");
    
    pmm_region_t *region = region_list;
    int count = 0;
    while (region) {
        char buf[16];
        serial_puts("Region ");
        itoa(count++, buf, 10);
        serial_puts(buf);
        serial_puts(": 0x");
        itoa(region->start_addr, buf, 16);
        serial_puts(buf);
        serial_puts(" - 0x");
        itoa(region->end_addr, buf, 16);
        serial_puts(buf);
        serial_puts(" Type: ");
        itoa(region->type, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        region = region->next;
    }
    
    if (count == 0) {
        serial_puts("No memory regions registered\n");
    }
    serial_puts("===========================\n\n");
}

void pmm_print_detailed_stats(void) {
    char buf[16];
    
    serial_puts("\n=== PMM Detailed Statistics ===\n");
    
    serial_puts("Total Physical Memory: ");
    itoa(total_frames * PAGE_SIZE / 1024 / 1024, buf, 10);
    serial_puts(buf);
    serial_puts(" MB (");
    itoa(total_frames, buf, 10);
    serial_puts(buf);
    serial_puts(" frames)\n");
    
    serial_puts("Used Memory: ");
    itoa(used_frames * PAGE_SIZE / 1024 / 1024, buf, 10);
    serial_puts(buf);
    serial_puts(" MB (");
    itoa(used_frames, buf, 10);
    serial_puts(buf);
    serial_puts(" frames)\n");
    
    serial_puts("Free Memory: ");
    uint32_t free = total_frames - used_frames;
    itoa(free * PAGE_SIZE / 1024 / 1024, buf, 10);
    serial_puts(buf);
    serial_puts(" MB (");
    itoa(free, buf, 10);
    serial_puts(buf);
    serial_puts(" frames)\n");
    
    serial_puts("\nAllocation Statistics:\n");
    serial_puts("  Total Allocations: ");
    itoa(alloc_count, buf, 10);
    serial_puts(buf);
    serial_puts("\n  Total Frees: ");
    itoa(free_count, buf, 10);
    serial_puts(buf);
    serial_puts("\n  Failed Allocations: ");
    itoa(failed_alloc_count, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("\nMemory Zones:\n");
    const char *zone_names[] = {"DMA (0-16MB)", "Normal (16-896MB)", "High (896MB+)"};
    for (int i = 0; i < PMM_ZONE_COUNT; i++) {
        if (zones[i].total_frames == 0) continue;
        
        serial_puts("  ");
        serial_puts(zone_names[i]);
        serial_puts(": ");
        itoa(zones[i].total_frames, buf, 10);
        serial_puts(buf);
        serial_puts(" frames, Reserved: ");
        itoa(zones[i].reserved_frames, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
    }
    
    serial_puts("===============================\n\n");
}

int pmm_validate_integrity(void) {
    int errors = 0;
    
    serial_puts("PMM: Running integrity check...\n");
    
    // Check that used_frames count matches bitmap
    uint32_t counted_used = 0;
    for (uint32_t i = 0; i < (total_frames + 31) / 32; i++) {
        uint32_t word = frame_bitmap[i];
        while (word) {
            counted_used += word & 1;
            word >>= 1;
        }
    }
    
    if (counted_used != used_frames) {
        serial_puts("ERROR: used_frames mismatch! Counted: ");
        char buf[16];
        itoa(counted_used, buf, 10);
        serial_puts(buf);
        serial_puts(" Tracked: ");
        itoa(used_frames, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        errors++;
    }
    
    // Check that reserved frames are marked as used
    for (uint32_t i = 0; i < KERNEL_RESERVED; i++) {
        if (!test_frame(i)) {
            serial_puts("ERROR: Reserved frame not marked as used: ");
            char buf[16];
            itoa(i, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
            errors++;
            if (errors > 10) break; // Limit error output
        }
    }
    
    if (errors == 0) {
        serial_puts("PMM: Integrity check passed!\n");
    } else {
        char buf[16];
        serial_puts("PMM: Integrity check found ");
        itoa(errors, buf, 10);
        serial_puts(buf);
        serial_puts(" errors\n");
    }
    
    return errors;
}

void pmm_dump_allocations(void) {
    serial_puts("\n=== PMM Allocation Dump ===\n");
    serial_puts("First 100 allocated frames:\n");
    
    int count = 0;
    char buf[16];
    
    for (uint32_t frame = 0; frame < total_frames && count < 100; frame++) {
        if (test_frame(frame)) {
            if (count % 10 == 0 && count > 0) {
                serial_puts("\n");
            }
            itoa(frame, buf, 10);
            serial_puts(buf);
            serial_puts(" ");
            count++;
        }
    }
    
    serial_puts("\nTotal shown: ");
    itoa(count, buf, 10);
    serial_puts(buf);
    serial_puts(" / ");
    itoa(used_frames, buf, 10);
    serial_puts(buf);
    serial_puts(" used frames\n");
    serial_puts("===========================\n\n");
}

