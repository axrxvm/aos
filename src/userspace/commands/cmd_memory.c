/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_memory.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <pmm.h>
#include <vmm.h>
#include <process.h>
#include <shell.h>
#include <mem_debug.h>

// Forward declarations
extern void kprint(const char *str);
extern uint32_t total_memory_kb;
extern address_space_t* kernel_address_space;
extern page_directory_t* current_directory;

// Enhanced memory overview with zone breakdown
static void cmd_mem(const char* args) {
    (void)args;
    char buf[32];
    
    // Header
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== System Memory Overview ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    // Total memory
    if (total_memory_kb > 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("Total Memory:  ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        itoa(total_memory_kb / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" MB");
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (");
        itoa(total_memory_kb, buf, 10);
        vga_puts(buf);
        vga_puts(" KB)");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    }
    
    // Physical memory stats
    uint32_t total = pmm_get_total_frames();
    uint32_t used = pmm_get_used_frames();
    uint32_t free = pmm_get_free_frames();
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Physical Used: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    itoa((used * 4096) / 1024, buf, 10);
    vga_puts(buf);
    vga_puts(" KB");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_puts(" / ");
    itoa((total * 4096) / 1024, buf, 10);
    vga_puts(buf);
    vga_puts(" KB total");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Physical Free: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    itoa((free * 4096) / 1024, buf, 10);
    vga_puts(buf);
    vga_puts(" KB");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_puts(" (");
    itoa((free * 100) / total, buf, 10);
    vga_puts(buf);
    vga_puts("%)");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    kprint("");
    
    // Zone breakdown
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("Memory Zones:");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    const char *zone_names[] = {"DMA", "Normal", "High"};
    for (int zone = 0; zone < PMM_ZONE_COUNT; zone++) {
        pmm_zone_stats_t stats;
        pmm_get_zone_stats((pmm_zone_t)zone, &stats);
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  ");
        vga_puts(zone_names[zone]);
        vga_puts(":      ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        itoa((stats.used_frames * 4096) / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" KB used");
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" / ");
        itoa((stats.total_frames * 4096) / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" KB total");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    }
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    kprint("Use 'mem-zones' for detailed zone info, 'mem-slabs' for slab stats");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

// Enhanced VMM status with slab allocator info
static void cmd_vmm(const char* args) {
    (void)args;
    char buf[32];
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== Virtual Memory Manager ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    if (kernel_address_space) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("Status:         ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kprint("Initialized");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("Paging:         ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kprint("Enabled");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("Page Directory: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        vga_puts("0x");
        itoa(current_directory->physical_addr, buf, 16);
        kprint(buf);
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        
        // Call detailed VMM stats
        vmm_print_detailed_stats();
        
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("VMM not initialized");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

// Comprehensive page allocation tests
static void cmd_test_page(const char* args) {
    (void)args;
    char buf[32];
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== Page Allocation Tests ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    // Test 1: Basic allocation
    kprint("Test 1: Basic page allocation");
    void *page1 = alloc_page();
    void *page2 = alloc_page();
    
    if (page1 && page2) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Page 1: 0x");
        itoa((uint32_t)page1, buf, 16);
        vga_puts(buf);
        vga_puts(" - ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kprint("OK");
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Page 2: 0x");
        itoa((uint32_t)page2, buf, 16);
        vga_puts(buf);
        vga_puts(" - ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kprint("OK");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        free_page(page1);
        free_page(page2);
        kprint("  Freed successfully");
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("  FAILED - Out of memory");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    kprint("");
    
    // Test 2: Zone-specific allocation
    kprint("Test 2: DMA zone allocation");
    void *dma_page = alloc_page_from_zone(PMM_ZONE_DMA);
    if (dma_page) {
        uint32_t dma_addr = (uint32_t)dma_page;
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  DMA Page: 0x");
        itoa(dma_addr, buf, 16);
        vga_puts(buf);
        if (dma_addr < 0x1000000) {
            vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
            kprint(" - OK (within DMA zone)");
        } else {
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            kprint(" - ERROR (outside DMA zone!)");
        }
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        free_page(dma_page);
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("  FAILED - No DMA memory available");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    kprint("");
    
    // Test 3: Contiguous allocation
    kprint("Test 3: Contiguous page allocation (4 pages)");
    void *contig = alloc_pages_contiguous(4);
    if (contig) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Start: 0x");
        itoa((uint32_t)contig, buf, 16);
        vga_puts(buf);
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kprint(" - OK");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        // Note: Would need to free all 4 pages individually
        for (int i = 0; i < 4; i++) {
            free_page((void*)((uint32_t)contig + i * 4096));
        }
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        kprint("  SKIPPED - Not enough contiguous memory");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    kprint("");
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprint("All page allocation tests completed!");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_showmem(const char* args) {
    (void)args;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("Memory Usage:");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    uint32_t total_frames = pmm_get_total_frames();
    uint32_t used_frames = pmm_get_used_frames();
    uint32_t free_frames = total_frames - used_frames;
    
    char num_str[16];
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Physical: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    itoa(used_frames * 4, num_str, 10);
    vga_puts(num_str);
    vga_puts(" KB");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts(" used, ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    itoa(free_frames * 4, num_str, 10);
    vga_puts(num_str);
    vga_puts(" KB");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts(" available");
    kprint("");
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_puts("Kernel Heap: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_puts("2 MB");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_puts(" allocated (0x500000 - 0x700000)");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    process_t* current = process_get_current();
    if (current) {
        kprint("");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("Current Task: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_puts(current->name);
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (TID ");
        itoa(current->pid, num_str, 10);
        vga_puts(num_str);
        vga_puts(")");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    }
}

// Detailed zone statistics
static void cmd_mem_zones(const char* args) {
    (void)args;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== Memory Zone Statistics ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    const char *zone_names[] = {"DMA Zone (0-16MB)", "Normal Zone (16-896MB)", "High Zone (896MB+)"};
    char buf[32];
    
    for (int zone = 0; zone < PMM_ZONE_COUNT; zone++) {
        pmm_zone_stats_t stats;
        pmm_get_zone_stats((pmm_zone_t)zone, &stats);
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        kprint(zone_names[zone]);
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Total Frames:    ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        itoa(stats.total_frames, buf, 10);
        vga_puts(buf);
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (");
        itoa((stats.total_frames * 4096) / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" KB)");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Used Frames:     ");
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        itoa(stats.used_frames, buf, 10);
        vga_puts(buf);
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (");
        itoa((stats.used_frames * 4096) / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" KB)");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        
        uint32_t free_frames = stats.total_frames - stats.used_frames - stats.reserved_frames;
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Free Frames:     ");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        itoa(free_frames, buf, 10);
        vga_puts(buf);
        vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_puts(" (");
        itoa((free_frames * 4096) / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" KB)");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Reserved Frames: ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        itoa(stats.reserved_frames, buf, 10);
        vga_puts(buf);
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_puts("  Frame Range:     ");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        itoa(stats.start_frame, buf, 10);
        vga_puts(buf);
        vga_puts(" - ");
        itoa(stats.end_frame, buf, 10);
        vga_puts(buf);
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        
        if (stats.total_frames > 0) {
            uint32_t usage_pct = (stats.used_frames * 100) / stats.total_frames;
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_puts("  Usage:           ");
            if (usage_pct > 90) {
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            } else if (usage_pct > 70) {
                vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            } else {
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            }
            itoa(usage_pct, buf, 10);
            vga_puts(buf);
            vga_puts("%");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            kprint("");
        }
        kprint("");
    }
}

// Slab allocator statistics
static void cmd_mem_slabs(const char* args) {
    (void)args;
    char buf[32];
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== Slab Allocator Statistics ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    if (!kernel_address_space) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("Kernel address space not initialized");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }
    
    const uint32_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    kprint("Size   Total   Free   Used   Slabs   Efficiency");
    kprint("----   -----   ----   ----   -----   ----------");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    for (int i = 0; i < NUM_SLAB_CACHES; i++) {
        slab_cache_t *cache = &kernel_address_space->slab_caches[i];
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        itoa(sizes[i], buf, 10);
        vga_puts(buf);
        if (sizes[i] < 1000) vga_puts(" ");
        if (sizes[i] < 100) vga_puts(" ");
        if (sizes[i] < 10) vga_puts(" ");
        vga_puts("  ");
        
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        itoa(cache->total_objects, buf, 10);
        vga_puts(buf);
        if (cache->total_objects < 1000) vga_puts(" ");
        if (cache->total_objects < 100) vga_puts(" ");
        if (cache->total_objects < 10) vga_puts(" ");
        vga_puts("   ");
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        itoa(cache->free_objects, buf, 10);
        vga_puts(buf);
        if (cache->free_objects < 1000) vga_puts(" ");
        if (cache->free_objects < 100) vga_puts(" ");
        if (cache->free_objects < 10) vga_puts(" ");
        vga_puts("  ");
        
        uint32_t used = cache->total_objects - cache->free_objects;
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        itoa(used, buf, 10);
        vga_puts(buf);
        if (used < 1000) vga_puts(" ");
        if (used < 100) vga_puts(" ");
        if (used < 10) vga_puts(" ");
        vga_puts("  ");
        
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        itoa(cache->total_slabs, buf, 10);
        vga_puts(buf);
        if (cache->total_slabs < 1000) vga_puts(" ");
        if (cache->total_slabs < 100) vga_puts(" ");
        if (cache->total_slabs < 10) vga_puts(" ");
        vga_puts("   ");
        
        if (cache->total_objects > 0) {
            uint32_t efficiency = (used * 100) / cache->total_objects;
            if (efficiency > 75) {
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            } else if (efficiency > 50) {
                vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            } else {
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            }
            itoa(efficiency, buf, 10);
            vga_puts(buf);
            vga_puts("%");
        } else {
            vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            vga_puts("N/A");
        }
        
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
    }
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    kprint("Note: Efficiency = (Used / Total) * 100%");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

// Memory debugging and integrity checks
static void cmd_mem_debug(const char* args) {
    (void)args;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== Memory Debug Information ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    // PMM integrity check
    kprint("Running PMM integrity check...");
    int pmm_ok = pmm_validate_integrity();
    if (pmm_ok) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("  PMM: OK - No corruption detected");
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("  PMM: ERROR - Corruption detected!");
    }
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    // VMM integrity check
    kprint("Running VMM integrity check...");
    int vmm_ok = vmm_validate_integrity();
    if (vmm_ok) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("  VMM: OK - No corruption detected");
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("  VMM: ERROR - Corruption detected!");
    }
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    // Memory leak check
    kprint("Checking for memory leaks...");
    mem_debug_check_leaks();
    kprint("");
    
    // Overall status
    if (pmm_ok && vmm_ok) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("All memory subsystems healthy!");
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("WARNING: Memory corruption detected!");
        kprint("Consider rebooting or investigating further.");
    }
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

// Run comprehensive memory tests
static void cmd_mem_test(const char* args) {
    (void)args;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== Memory System Tests ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    kprint("Warning: This will run extensive tests and may take time.");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    // Run all memory tests
    mem_run_all_tests();
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprint("All tests completed! Check output above for results.");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void cmd_module_memory_register(void) {
    // Basic memory commands
    command_register_with_category("mem", "", "Display system memory overview", "Memory", cmd_mem);
    command_register_with_category("vmm", "", "Display virtual memory status", "Memory", cmd_vmm);
    command_register_with_category("showmem", "", "Display memory usage summary", "Memory", cmd_showmem);
    
    // Advanced commands
    command_register_with_category("mem-zones", "", "Show detailed zone statistics", "Memory", cmd_mem_zones);
    command_register_with_category("mem-slabs", "", "Show slab allocator statistics", "Memory", cmd_mem_slabs);
    command_register_with_category("mem-debug", "", "Run memory integrity checks", "Memory", cmd_mem_debug);
    
    // Test commands
    command_register_with_category("test-page", "", "Test page allocation", "Memory", cmd_test_page);
    command_register_with_category("mem-test", "", "Run comprehensive memory tests", "Memory", cmd_mem_test);
}
