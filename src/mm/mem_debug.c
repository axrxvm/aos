/*
 * === AOS HEADER BEGIN ===
 * src/mm/mem_debug.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <mem_debug.h>
#include <serial.h>
#include <stdlib.h>
#include <string.h>
#include <pmm.h>
#include <vmm.h>

// Allocation tracking
static mem_alloc_record_t alloc_records[MAX_ALLOC_RECORDS];
static int alloc_record_count = 0;
static int initialized = 0;

// Statistics
static uint32_t total_allocs = 0;
static uint32_t total_frees = 0;
static uint32_t double_free_detections = 0;
static uint32_t invalid_free_detections = 0;
static uint32_t corruption_detections = 0;

// Performance profiling
static uint32_t profile_start_time = 0;
static uint32_t profile_allocs = 0;
static uint32_t profile_frees = 0;

void mem_debug_init(void) {
    if (initialized) return;
    
    serial_puts("Initializing memory debugging system...\n");
    
    // Clear allocation records
    for (int i = 0; i < MAX_ALLOC_RECORDS; i++) {
        alloc_records[i].ptr = NULL;
        alloc_records[i].size = 0;
        alloc_records[i].file = NULL;
        alloc_records[i].line = 0;
        alloc_records[i].timestamp = 0;
        alloc_records[i].active = 0;
    }
    
    alloc_record_count = 0;
    total_allocs = 0;
    total_frees = 0;
    double_free_detections = 0;
    invalid_free_detections = 0;
    corruption_detections = 0;
    
    initialized = 1;
    serial_puts("Memory debugging initialized successfully\n");
}

void mem_debug_record_alloc(void *ptr, size_t size, const char *file, int line) {
    if (!initialized || !ptr) return;
    
    // Find free slot
    for (int i = 0; i < MAX_ALLOC_RECORDS; i++) {
        if (!alloc_records[i].active) {
            alloc_records[i].ptr = ptr;
            alloc_records[i].size = size;
            alloc_records[i].file = file;
            alloc_records[i].line = line;
            alloc_records[i].timestamp = total_allocs;
            alloc_records[i].active = 1;
            alloc_record_count++;
            total_allocs++;
            
            #if MEM_DEBUG_VERBOSE
            serial_puts("ALLOC: ");
            char buf[16];
            itoa((uint32_t)(uintptr_t)ptr, buf, 16);
            serial_puts(buf);
            serial_puts(" size=");
            itoa(size, buf, 10);
            serial_puts(buf);
            serial_puts(" at ");
            serial_puts(file);
            serial_puts(":");
            itoa(line, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
            #endif
            
            return;
        }
    }
    
    serial_puts("WARNING: Allocation record pool exhausted\n");
}

void mem_debug_record_free(void *ptr, const char *file, int line) {
    if (!initialized || !ptr) return;
    
    // Find and deactivate the record
    int found = 0;
    for (int i = 0; i < MAX_ALLOC_RECORDS; i++) {
        if (alloc_records[i].active && alloc_records[i].ptr == ptr) {
            if (found) {
                // Duplicate entry - this is a bug!
                serial_puts("ERROR: Duplicate allocation record found!\n");
            }
            alloc_records[i].active = 0;
            alloc_record_count--;
            total_frees++;
            found = 1;
            
            #if MEM_DEBUG_VERBOSE
            serial_puts("FREE: ");
            char buf[16];
            itoa((uint32_t)ptr, buf, 16);
            serial_puts(buf);
            serial_puts(" (was allocated at ");
            serial_puts(alloc_records[i].file);
            serial_puts(":");
            itoa(alloc_records[i].line, buf, 10);
            serial_puts(buf);
            serial_puts(")\n");
            #endif
        }
    }
    
    if (!found) {
        serial_puts("ERROR: Free of untracked pointer: 0x");
        char buf[16];
        itoa((uint32_t)(uintptr_t)ptr, buf, 16);
        serial_puts(buf);
        serial_puts(" at ");
        serial_puts(file);
        serial_puts(":");
        itoa(line, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        invalid_free_detections++;
    }
}

void mem_debug_check_leaks(void) {
    if (!initialized) return;
    
    serial_puts("\n=== Memory Leak Detection ===\n");
    
    int leak_count = 0;
    size_t total_leaked = 0;
    
    for (int i = 0; i < MAX_ALLOC_RECORDS; i++) {
        if (alloc_records[i].active) {
            leak_count++;
            total_leaked += alloc_records[i].size;
            
            serial_puts("LEAK: ");
            char buf[16];
            itoa((uint32_t)(uintptr_t)alloc_records[i].ptr, buf, 16);
            serial_puts(buf);
            serial_puts(" size=");
            itoa(alloc_records[i].size, buf, 10);
            serial_puts(buf);
            serial_puts(" bytes allocated at ");
            if (alloc_records[i].file) {
                serial_puts(alloc_records[i].file);
                serial_puts(":");
                itoa(alloc_records[i].line, buf, 10);
                serial_puts(buf);
            } else {
                serial_puts("(unknown)");
            }
            serial_puts("\n");
        }
    }
    
    if (leak_count == 0) {
        serial_puts("No memory leaks detected!\n");
    } else {
        char buf[16];
        serial_puts("Found ");
        itoa(leak_count, buf, 10);
        serial_puts(buf);
        serial_puts(" memory leaks totaling ");
        itoa(total_leaked, buf, 10);
        serial_puts(buf);
        serial_puts(" bytes\n");
    }
    
    serial_puts("=============================\n\n");
}

void mem_debug_print_allocations(void) {
    if (!initialized) return;
    
    serial_puts("\n=== Active Allocations ===\n");
    
    char buf[16];
    serial_puts("Total active allocations: ");
    itoa(alloc_record_count, buf, 10);
    serial_puts(buf);
    serial_puts("\n\n");
    
    int shown = 0;
    for (int i = 0; i < MAX_ALLOC_RECORDS && shown < 20; i++) {
        if (alloc_records[i].active) {
            serial_puts("[");
            itoa(shown + 1, buf, 10);
            serial_puts(buf);
            serial_puts("] 0x");
            itoa((uint32_t)(uintptr_t)alloc_records[i].ptr, buf, 16);
            serial_puts(buf);
            serial_puts(" (");
            itoa(alloc_records[i].size, buf, 10);
            serial_puts(buf);
            serial_puts(" bytes) at ");
            if (alloc_records[i].file) {
                serial_puts(alloc_records[i].file);
                serial_puts(":");
                itoa(alloc_records[i].line, buf, 10);
                serial_puts(buf);
            }
            serial_puts("\n");
            shown++;
        }
    }
    
    if (alloc_record_count > shown) {
        serial_puts("... and ");
        itoa(alloc_record_count - shown, buf, 10);
        serial_puts(buf);
        serial_puts(" more\n");
    }
    
    serial_puts("==========================\n\n");
}

void mem_debug_dump_stats(void) {
    if (!initialized) return;
    
    char buf[16];
    
    serial_puts("\n=== Memory Debug Statistics ===\n");
    
    serial_puts("Total Allocations: ");
    itoa(total_allocs, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("Total Frees: ");
    itoa(total_frees, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("Active Allocations: ");
    itoa(alloc_record_count, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("Double-Free Detections: ");
    itoa(double_free_detections, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("Invalid Free Detections: ");
    itoa(invalid_free_detections, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("Corruption Detections: ");
    itoa(corruption_detections, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("================================\n\n");
}

int mem_debug_check_heap_integrity(void) {
    if (!initialized) return 0;
    
    int errors = 0;
    
    serial_puts("Checking heap integrity...\n");
    
    // Check PMM integrity
    errors += pmm_validate_integrity();
    
    // Check VMM integrity
    errors += vmm_validate_integrity();
    
    // Check all tracked allocations for corruption
    for (int i = 0; i < MAX_ALLOC_RECORDS; i++) {
        if (alloc_records[i].active) {
            if (!vmm_check_guards(alloc_records[i].ptr)) {
                serial_puts("ERROR: Guard corruption detected in allocation at 0x");
                char buf[16];
                itoa((uint32_t)(uintptr_t)alloc_records[i].ptr, buf, 16);
                serial_puts(buf);
                serial_puts("\n");
                errors++;
                corruption_detections++;
            }
        }
    }
    
    if (errors == 0) {
        serial_puts("Heap integrity check: PASSED\n");
    } else {
        char buf[16];
        serial_puts("Heap integrity check: FAILED (");
        itoa(errors, buf, 10);
        serial_puts(buf);
        serial_puts(" errors)\n");
    }
    
    return errors;
}

int mem_debug_validate_memory_range(void *start, size_t size) {
    if (!start || size == 0) return 0;
    
    uintptr_t addr = (uintptr_t)start;
    uintptr_t end = addr + size;
    
    // Check if range is in valid memory
    if (addr < 0x1000) {
        serial_puts("ERROR: Memory range in null pointer range\n");
        return 0;
    }
    
    // Check for overflow
    if (end < addr) {
        serial_puts("ERROR: Memory range overflow\n");
        return 0;
    }
    
    // Validate all pages in range are mapped
    for (uintptr_t page = addr; page < end; page += 4096) {
        if (!vmm_validate_pointer((void*)page)) {
            serial_puts("ERROR: Memory range contains unmapped page at 0x");
            char buf[16];
            itoa((uint32_t)page, buf, 16);
            serial_puts(buf);
            serial_puts("\n");
            return 0;
        }
    }
    
    return 1;
}

void mem_debug_start_profile(void) {
    profile_start_time = total_allocs + total_frees;
    profile_allocs = 0;
    profile_frees = 0;
    serial_puts("Memory profiling started\n");
}

void mem_debug_stop_profile(void) {
    profile_allocs = total_allocs;
    profile_frees = total_frees;
    serial_puts("Memory profiling stopped\n");
}

void mem_debug_print_profile(void) {
    char buf[16];
    
    serial_puts("\n=== Memory Profile ===\n");
    serial_puts("Allocations during profile: ");
    itoa(profile_allocs, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("Frees during profile: ");
    itoa(profile_frees, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("Net allocations: ");
    itoa(profile_allocs - profile_frees, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("======================\n\n");
}

// Memory tests
void mem_test_allocator(void) {
    serial_puts("\n=== Testing Memory Allocator ===\n");
    
    // Test 1: Small allocations
    serial_puts("Test 1: Small allocations... ");
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(32);
        if (!ptrs[i]) {
            serial_puts("FAILED (allocation)\n");
            return;
        }
    }
    for (int i = 0; i < 10; i++) {
        kfree(ptrs[i]);
    }
    serial_puts("PASSED\n");
    
    // Test 2: Large allocation
    serial_puts("Test 2: Large allocation... ");
    void *large = kmalloc(8192);
    if (!large) {
        serial_puts("FAILED\n");
        return;
    }
    kfree(large);
    serial_puts("PASSED\n");
    
    // Test 3: NULL handling
    serial_puts("Test 3: NULL handling... ");
    kfree(NULL);
    serial_puts("PASSED\n");
    
    serial_puts("================================\n\n");
}

void mem_test_slab_allocator(void) {
    serial_puts("\n=== Testing Slab Allocator ===\n");
    
    // Test allocations of various slab sizes
    serial_puts("Testing slab sizes... ");
    void *ptrs[100];
    int sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    
    for (int s = 0; s < 9; s++) {
        for (int i = 0; i < 10; i++) {
            ptrs[i] = kmalloc(sizes[s]);
            if (!ptrs[i]) {
                serial_puts("FAILED (alloc size=");
                char buf[16];
                itoa(sizes[s], buf, 10);
                serial_puts(buf);
                serial_puts(")\n");
                return;
            }
            // Write to memory to test it's valid
            memset(ptrs[i], 0xAA, sizes[s]);
        }
        
        for (int i = 0; i < 10; i++) {
            kfree(ptrs[i]);
        }
    }
    
    serial_puts("PASSED\n");
    serial_puts("===============================\n\n");
}

void mem_test_page_allocator(void) {
    serial_puts("\n=== Testing Page Allocator ===\n");
    
    serial_puts("Allocating pages... ");
    void *pages[10];
    for (int i = 0; i < 10; i++) {
        pages[i] = alloc_page();
        if (!pages[i]) {
            serial_puts("FAILED\n");
            return;
        }
    }
    
    serial_puts("Freeing pages... ");
    for (int i = 0; i < 10; i++) {
        free_page(pages[i]);
    }
    
    serial_puts("PASSED\n");
    serial_puts("===============================\n\n");
}

void mem_test_guards(void) {
    serial_puts("\n=== Testing Memory Guards ===\n");
    
    serial_puts("Allocating with guards... ");
    void *ptr = kmalloc(64);
    if (!ptr) {
        serial_puts("FAILED (allocation)\n");
        return;
    }
    
    serial_puts("Checking guards... ");
    if (!vmm_check_guards(ptr)) {
        serial_puts("FAILED (guard check)\n");
        kfree(ptr);
        return;
    }
    
    kfree(ptr);
    serial_puts("PASSED\n");
    serial_puts("==============================\n\n");
}

void mem_run_all_tests(void) {
    serial_puts("\n");
    serial_puts("=====================================\n");
    serial_puts("  RUNNING MEMORY SYSTEM TESTS\n");
    serial_puts("=====================================\n");
    
    mem_test_allocator();
    mem_test_slab_allocator();
    mem_test_page_allocator();
    mem_test_guards();
    
    serial_puts("=====================================\n");
    serial_puts("  ALL MEMORY TESTS COMPLETED\n");
    serial_puts("=====================================\n\n");
}
