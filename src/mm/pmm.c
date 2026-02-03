/*
 * === AOS HEADER BEGIN ===
 * ./src/mm/pmm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


// src/pmm.c
#include <stdint.h>
#include <serial.h> // Include for serial_puts
#include <panic.h>
#include <memory.h>
#include <pmm.h>
#include <stdlib.h> // For itoa

static uint32_t frame_bitmap[MAX_FRAMES / 32];
static uint32_t total_frames = 0;

void set_frame(uint32_t frame_index) {
    if (frame_index >= MAX_FRAMES) {
        serial_puts("WARNING: set_frame - frame_index out of bounds\n");
        return;
    }
    uint32_t index = frame_index / 32;
    uint32_t offset = frame_index % 32;
    frame_bitmap[index] |= (1 << offset);
}

void clear_frame(uint32_t frame_index) {
    if (frame_index >= MAX_FRAMES) {
        serial_puts("WARNING: clear_frame - frame_index out of bounds\n");
        return;
    }
    uint32_t index = frame_index / 32;
    uint32_t offset = frame_index % 32;
    frame_bitmap[index] &= ~(1 << offset);
}

uint8_t test_frame(uint32_t frame_index) {
    if (frame_index >= MAX_FRAMES) {
        serial_puts("WARNING: test_frame - frame_index out of bounds\n");
        return 1; // Return as allocated to prevent allocation
    }
    uint32_t index = frame_index / 32;
    uint32_t offset = frame_index % 32;
    return (frame_bitmap[index] & (1 << offset)) != 0;
}

int find_first_free_frame() {
    // Don't allocate the first 512 frames (2MB) - reserved for kernel (~33KB), BIOS (1MB), and early heap
    uint32_t start_frame = 512;
    uint32_t end_bitmap = (total_frames + 31) / 32;
    
    for (uint32_t i = start_frame / 32; i < end_bitmap; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (frame >= start_frame && frame < total_frames && !(frame_bitmap[i] & (1 << j))) {
                    return frame;
                }
            }
        }
    }
    return -1;
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

    serial_puts("PMM initialized successfully.\n");
}

void* alloc_page() {
    int frame = find_first_free_frame();
    if (frame == -1) {
        panic("Out of physical memory!");
    }
    set_frame(frame);
    return (void*)(frame * PAGE_SIZE);
}

void free_page(void* page) {
    uint32_t frame = (uint32_t)page / PAGE_SIZE;
    
    // Validate frame is within bounds and not in reserved area
    if (frame >= total_frames || frame >= MAX_FRAMES) {
        serial_puts("WARNING: free_page - frame out of bounds\n");
        return;
    }
    
    if (frame < 512) {
        serial_puts("WARNING: free_page - attempt to free reserved frame\n");
        return;
    }
    
    clear_frame(frame);
    // serial_puts("Page freed.\n");
}

// Statistics functions
uint32_t pmm_get_total_frames(void) {
    return total_frames;
}

uint32_t pmm_get_used_frames(void) {
    uint32_t used = 0;
    uint32_t end_bitmap = (total_frames + 31) / 32;
    
    // Validate end_bitmap is within bounds
    if (end_bitmap > (MAX_FRAMES / 32)) {
        end_bitmap = MAX_FRAMES / 32;
    }
    
    for (uint32_t i = 0; i < end_bitmap; i++) {
        uint32_t word = frame_bitmap[i];
        // Count set bits
        while (word) {
            used += word & 1;
            word >>= 1;
        }
    }
    
    return used;
}

uint32_t pmm_get_free_frames(void) {
    return total_frames - pmm_get_used_frames();
}
