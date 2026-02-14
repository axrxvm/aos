/*
 * === AOS HEADER BEGIN ===
 * include/mem_debug.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef MEM_DEBUG_H
#define MEM_DEBUG_H

#include <stdint.h>
#include <stddef.h>

// Memory debugging configuration
#define MEM_DEBUG_ENABLED 1
#define MEM_DEBUG_VERBOSE 0
#define MEM_DEBUG_TRACK_BACKTRACE 0

// Memory leak detection
#define MAX_ALLOC_RECORDS 1024

typedef struct mem_alloc_record {
    void *ptr;
    size_t size;
    const char *file;
    int line;
    uint32_t timestamp;
    int active;
} mem_alloc_record_t;

// Memory debugging functions
void mem_debug_init(void);
void mem_debug_record_alloc(void *ptr, size_t size, const char *file, int line);
void mem_debug_record_free(void *ptr, const char *file, int line);
void mem_debug_check_leaks(void);
void mem_debug_print_allocations(void);
void mem_debug_dump_stats(void);

// Memory corruption detection
int mem_debug_check_heap_integrity(void);
int mem_debug_validate_memory_range(void *start, size_t size);

// Performance monitoring
void mem_debug_start_profile(void);
void mem_debug_stop_profile(void);
void mem_debug_print_profile(void);

// Debug macros
#if MEM_DEBUG_ENABLED
#define DEBUG_ALLOC(ptr, size) mem_debug_record_alloc(ptr, size, __FILE__, __LINE__)
#define DEBUG_FREE(ptr) mem_debug_record_free(ptr, __FILE__, __LINE__)
#else
#define DEBUG_ALLOC(ptr, size) ((void)0)
#define DEBUG_FREE(ptr) ((void)0)
#endif

// Memory test functions
void mem_test_allocator(void);
void mem_test_slab_allocator(void);
void mem_test_page_allocator(void);
void mem_test_guards(void);
void mem_run_all_tests(void);

#endif // MEM_DEBUG_H
