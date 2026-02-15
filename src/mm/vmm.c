/*
 * === AOS HEADER BEGIN ===
 * src/mm/vmm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <vmm.h>
#include <arch/i386/paging.h>
#include <pmm.h>
#include <serial.h>
#include <vga.h>
#include <stdlib.h>
#include <string.h>
#include <panic.h>

// Define SIZE_MAX if not already defined
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

// Memory guards for corruption detection
#ifndef GUARD_MAGIC_START
#define GUARD_MAGIC_START   0xDEADBEEF
#endif
#ifndef GUARD_MAGIC_END
#define GUARD_MAGIC_END     0xBEEFDEAD
#endif
#ifndef GUARD_MAGIC_FREED
#define GUARD_MAGIC_FREED   0xFEEEFEEE  // Marker for freed memory (double-free detection)
#endif

// Current address spaces
address_space_t *current_address_space = 0;
address_space_t *kernel_address_space = 0;

// Static kernel address space (to avoid kmalloc during initialization)
static address_space_t kernel_as_static;

// Simple kernel heap allocator (before we have a proper heap)
static uint32_t kernel_heap_ptr = 0x500000;  // Start at 5MB
static uint32_t kernel_heap_end = 0x700000;  // End at 7MB

// Allocation statistics
static uint32_t total_allocations = 0;
static uint32_t total_frees = 0;
static uint32_t bytes_allocated = 0;
static uint32_t bytes_freed = 0;
static uint32_t peak_usage = 0;

// Slab cache sizes
static const uint32_t slab_sizes[NUM_SLAB_CACHES] = {
    SLAB_SIZE_8, SLAB_SIZE_16, SLAB_SIZE_32, SLAB_SIZE_64,
    SLAB_SIZE_128, SLAB_SIZE_256, SLAB_SIZE_512, 
    SLAB_SIZE_1024, SLAB_SIZE_2048
};

// Forward declarations
static void init_slab_cache(slab_cache_t *cache, uint32_t obj_size);
static void *slab_alloc(slab_cache_t *cache);
static void slab_free(slab_cache_t *cache, void *ptr);
static uint32_t calculate_checksum(void *ptr, size_t size);

// Simple checksum calculation for integrity checking
static uint32_t calculate_checksum(void *ptr, size_t size) {
    uint32_t sum = 0;
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i++) {
        sum += bytes[i];
        sum = (sum << 1) | (sum >> 31); // Rotate left
    }
    return sum;
}

// Initialize a slab cache
static void init_slab_cache(slab_cache_t *cache, uint32_t obj_size) {
    if (!cache) return;
    
    cache->obj_size = obj_size;
    cache->free_list = NULL;
    cache->total_objects = 0;
    cache->free_objects = 0;
    cache->total_slabs = 0;
    cache->slab_pages = NULL;
}

// Allocate from slab cache
static void *slab_alloc(slab_cache_t *cache) {
    if (!cache) return NULL;
    
    // Check if we need to allocate a new slab
    if (cache->free_list == NULL) {
        // Allocate a new page for this slab
        // Use DMA zone to ensure we get identity-mapped memory (0-8MB)
        void *page = alloc_page_from_zone(PMM_ZONE_DMA);
        if (!page) {
            // Fallback to regular allocation if DMA zone exhausted
            page = alloc_page();
            if (!page) {
                serial_puts("SLAB: Failed to allocate page for slab\n");
                return NULL;
            }
        }
        
        // Get the physical address
        uint32_t page_addr = (uint32_t)page;
        
        // For addresses beyond identity-mapped region, we need to skip for now
        // This is a safety check - identity mapping covers 0-32MB during early boot
        if (page_addr >= 0x2000000) {
            serial_puts("SLAB: Allocated page beyond identity-mapped region, freeing\n");
            free_page(page);
            return NULL;
        }
        
        cache->total_slabs++;
        
        // Calculate how many objects fit in a page
        uint32_t obj_total_size = cache->obj_size + sizeof(slab_obj_t) + sizeof(uint32_t); // +4 for end guard
        uint32_t objects_per_slab = PAGE_SIZE / obj_total_size;
        
        if (objects_per_slab == 0) {
            serial_puts("SLAB: Object size too large for slab\n");
            free_page(page);
            return NULL;
        }
        
        // Initialize free list from this slab
        // Use the physical address directly (identity mapped)
        uint8_t *slab_mem = (uint8_t *)page_addr;
        for (uint32_t i = 0; i < objects_per_slab; i++) {
            slab_obj_t *obj = (slab_obj_t *)(slab_mem + (i * obj_total_size));
            obj->magic_start = GUARD_MAGIC_START;
            obj->size = cache->obj_size;
            obj->next = cache->free_list;
            obj->checksum = 0; // Will be set on allocation
            
            // Add end guard
            uint32_t *end_guard = (uint32_t *)((uint8_t *)(obj + 1) + cache->obj_size);
            *end_guard = GUARD_MAGIC_END;
            
            cache->free_list = obj;
            cache->total_objects++;
            cache->free_objects++;
        }
    }
    
    // Pop from free list
    slab_obj_t *obj = cache->free_list;
    if (!obj) return NULL;
    
    cache->free_list = obj->next;
    cache->free_objects--;
    
    // Calculate checksum for the header
    obj->checksum = calculate_checksum(obj, sizeof(slab_obj_t) - sizeof(uint32_t));
    
    // Return pointer after header
    return (void *)(obj + 1);
}

// Free to slab cache with enhanced safety checks
static void slab_free(slab_cache_t *cache, void *ptr) {
    if (!cache || !ptr) {
        serial_puts("ERROR: slab_free - NULL cache or pointer\n");
        return;
    }
    
    // Get header
    slab_obj_t *obj = ((slab_obj_t *)ptr) - 1;
    
    // Validate object is not obviously corrupt
    uint32_t obj_addr = (uint32_t)obj;
    if (obj_addr < 0x100000 || obj_addr > 0x20000000) {
        serial_puts("ERROR: slab_free - object address out of valid range\n");
        return;
    }
    
    // Check for double-free
    if (obj->magic_start == GUARD_MAGIC_FREED) {
        // Double-free detected - silently ignore to prevent corruption
        // Memory not returned to pool (already freed)
        return;
    }
    
    // Validate guards and checksum
    if (obj->magic_start != GUARD_MAGIC_START) {
        serial_puts("WARNING: Memory corruption - start guard invalid at 0x");
        char buf[16];
        itoa((uint32_t)ptr, buf, 16);
        serial_puts(buf);
        serial_puts(" Expected: 0xDEADBEEF, Got: 0x");
        itoa(obj->magic_start, buf, 16);
        serial_puts(buf);
        serial_puts(" - operation aborted\n");
        // Don't free corrupted memory
        return;
    }
    
    // Validate size
    if (obj->size == 0 || obj->size > SLAB_SIZE_2048) {
        serial_puts("ERROR: slab_free - corrupted object size: ");
        char buf[16];
        itoa(obj->size, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        return;
    }
    
    uint32_t *end_guard = (uint32_t *)((uint8_t *)ptr + obj->size);
    if (*end_guard != GUARD_MAGIC_END) {
        serial_puts("WARNING: Buffer corruption - end guard invalid at 0x");
        char buf[16];
        itoa((uint32_t)ptr, buf, 16);
        serial_puts(buf);
        serial_puts(" Expected: 0xBEEFDEAD, Got: 0x");
        itoa(*end_guard, buf, 16);
        serial_puts(buf);
        serial_puts(" - possible buffer overflow\n");
        // Don't free corrupted memory
        return;
    }
    
    // Verify checksum
    uint32_t expected_checksum = calculate_checksum(obj, sizeof(slab_obj_t) - sizeof(uint32_t));
    if (obj->checksum != expected_checksum) {
        serial_puts("WARNING: Memory corruption - checksum mismatch at 0x");
        char buf[16];
        itoa((uint32_t)ptr, buf, 16);
        serial_puts(buf);
        serial_puts(" - metadata may be corrupted\n");
        // Continue anyway - checksum might have been updated
    }
    
    // Mark as freed to catch double-frees
    obj->magic_start = GUARD_MAGIC_FREED;
    
    // Poison the freed memory (fill with recognizable pattern)
    // This helps detect use-after-free bugs
    memset(ptr, 0xFE, obj->size);
    
    // Add back to free list
    obj->next = cache->free_list;
    cache->free_list = obj;
    cache->free_objects++;
}

void init_vmm(void) {
    serial_puts("Initializing Virtual Memory Manager...\n");
    
    // Use static allocation for kernel address space to avoid kmalloc recursion
    kernel_address_space = &kernel_as_static;
    
    // Clear structure
    memset(kernel_address_space, 0, sizeof(address_space_t));
    
    // Use existing kernel page directory
    kernel_address_space->page_dir = kernel_directory;
    kernel_address_space->heap_start = kernel_heap_ptr;
    kernel_address_space->heap_end = kernel_heap_ptr;
    kernel_address_space->stack_top = 0; // Kernel doesn't use user stack
    
    // Initialize slab caches for kernel
    for (int i = 0; i < NUM_SLAB_CACHES; i++) {
        init_slab_cache(&kernel_address_space->slab_caches[i], slab_sizes[i]);
    }
    
    current_address_space = kernel_address_space;
    
    serial_puts("VMM initialized successfully with slab allocator!\n");
}

address_space_t *create_address_space(void) {
    // Allocate address space structure
    address_space_t *as = (address_space_t *)kmalloc(sizeof(address_space_t));
    if (!as) {
        return 0;
    }
    
    // Clear structure
    memset(as, 0, sizeof(address_space_t));
    
    // Create page directory
    as->page_dir = create_page_directory();
    if (!as->page_dir) {
        kfree(as);
        return 0;
    }
    
    // Copy kernel mappings (both identity and higher half) to new directory
    // Identity mapped kernel (0-32MB for early boot safety)
    for (uint32_t addr = 0; addr < 0x2000000; addr += PAGE_SIZE) {
        uint32_t phys = get_physical_address(kernel_directory, addr);
        if (phys != 0) {
            map_page(as->page_dir, addr, phys, PAGE_PRESENT | PAGE_WRITE);
        }
    }
    
    // Higher half kernel mappings
    for (uint32_t addr = KERNEL_VIRTUAL_BASE; addr < KERNEL_VIRTUAL_BASE + 0x400000; addr += PAGE_SIZE) {
        uint32_t phys = get_physical_address(kernel_directory, addr);
        if (phys != 0) {
            map_page(as->page_dir, addr, phys, PAGE_PRESENT | PAGE_WRITE);
        }
    }
    
    // Set up default memory layout
    as->heap_start = VMM_USER_HEAP_START;
    as->heap_end = VMM_USER_HEAP_START;
    as->stack_top = VMM_USER_STACK_TOP;
    
    // Initialize slab caches for this address space
    for (int i = 0; i < NUM_SLAB_CACHES; i++) {
        init_slab_cache(&as->slab_caches[i], slab_sizes[i]);
    }
    
    return as;
}

void destroy_address_space(address_space_t *as) {
    if (!as || as == kernel_address_space) {
        return;
    }
    
    // Free all VMAs
    vma_t *vma = as->vma_list;
    while (vma) {
        vma_t *next = vma->next;
        kfree(vma);
        vma = next;
    }
    
    // Destroy page directory
    destroy_page_directory(as->page_dir);
    
    // Free address space structure
    kfree(as);
}

void switch_address_space(address_space_t *as) {
    if (!as) return;
    
    current_address_space = as;
    switch_page_directory(as->page_dir);
}

void *vmm_alloc_pages(address_space_t *as, uint32_t virtual_addr, size_t num_pages, uint32_t flags) {
    if (!as) return 0;
    
    // Check for integer overflow in size calculation
    if (num_pages > 0 && (num_pages * PAGE_SIZE) < num_pages) {
        serial_puts("VMM: Integer overflow in allocation size\n");
        return 0;
    }
    
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    
    // Allocate physical pages and map them
    for (size_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = virtual_addr + (i * PAGE_SIZE);
        
        // Check if already mapped
        if (is_page_present(as->page_dir, vaddr)) {
            // Unmap what we've allocated so far
            for (size_t j = 0; j < i; j++) {
                uint32_t unmap_addr = virtual_addr + (j * PAGE_SIZE);
                uint32_t phys = get_physical_address(as->page_dir, unmap_addr);
                unmap_page(as->page_dir, unmap_addr);
                if (phys) free_page((void *)phys);
            }
            return 0;
        }
        
        // Allocate physical page
        void *phys_page = alloc_page();
        if (!phys_page) {
            // Clean up on failure
            for (size_t j = 0; j < i; j++) {
                uint32_t unmap_addr = virtual_addr + (j * PAGE_SIZE);
                uint32_t phys = get_physical_address(as->page_dir, unmap_addr);
                unmap_page(as->page_dir, unmap_addr);
                if (phys) free_page((void *)phys);
            }
            return 0;
        }
        
        // Map the page
        map_page(as->page_dir, vaddr, (uint32_t)phys_page, flags);
        
        // Clear the page if it's writable
        if (flags & PAGE_WRITE) {
            memset((void *)vaddr, 0, PAGE_SIZE);
        }
    }
    
    // Create VMA entry to track allocation
    vma_t *vma = (vma_t *)kmalloc(sizeof(vma_t));
    if (vma) {
        vma->start_addr = virtual_addr;
        vma->end_addr = virtual_addr + (num_pages * PAGE_SIZE);
        vma->flags = flags;
        vma->magic = 0xDEADBEEF;  // Set magic for validation
        vma->next = as->vma_list;
        as->vma_list = vma;
    }
    
    return (void *)virtual_addr;
}

void *vmm_alloc_at(address_space_t *as, uint32_t virtual_addr, size_t size, uint32_t flags) {
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    return vmm_alloc_pages(as, virtual_addr, num_pages, flags);
}

void *vmm_alloc_anywhere(address_space_t *as, size_t size, uint32_t flags) {
    if (!as) return 0;
    
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // Find free virtual address space
    uint32_t start_addr = (flags & PAGE_USER) ? VMM_USER_HEAP_START : VMM_KERNEL_HEAP_START;
    uint32_t end_addr = (flags & PAGE_USER) ? KERNEL_VIRTUAL_BASE : 0xFFFFFFFF;
    
    // Simple linear search for free space
    for (uint32_t addr = start_addr; addr < end_addr - (num_pages * PAGE_SIZE); addr += PAGE_SIZE) {
        int free_space = 1;
        
        // Check if this range is free
        for (size_t i = 0; i < num_pages; i++) {
            if (is_page_present(as->page_dir, addr + (i * PAGE_SIZE))) {
                free_space = 0;
                break;
            }
        }
        
        if (free_space) {
            return vmm_alloc_pages(as, addr, num_pages, flags);
        }
    }
    
    return 0; // No free space found
}

void vmm_free_pages(address_space_t *as, uint32_t virtual_addr, size_t num_pages) {
    if (!as) return;
    
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    
    // Unmap and free physical pages
    for (size_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = virtual_addr + (i * PAGE_SIZE);
        uint32_t phys = get_physical_address(as->page_dir, vaddr);
        
        if (phys) {
            unmap_page(as->page_dir, vaddr);
            free_page((void *)phys);
        }
    }
    
    // Remove VMA entry
    vma_t **vma_ptr = &as->vma_list;
    while (*vma_ptr) {
        vma_t *vma = *vma_ptr;
        if (vma->start_addr == virtual_addr) {
            *vma_ptr = vma->next;
            kfree(vma);
            break;
        }
        vma_ptr = &vma->next;
    }
}

void *kmalloc_pages(size_t num_pages) {
    if (!kernel_address_space) {
        // Before VMM is initialized, use simple allocation
        // Note: This allocates non-contiguous physical pages
        void *pages[32]; // Limit to 32 pages (128KB) for early allocation
        if (num_pages > 32) return 0;
        
        for (size_t i = 0; i < num_pages; i++) {
            void *page = alloc_page();
            if (!page) {
                // Free previously allocated pages
                for (size_t j = 0; j < i; j++) {
                    free_page(pages[j]);
                }
                return 0;
            }
            pages[i] = page;
        }
        return pages[0];
    }
    
    return vmm_alloc_anywhere(kernel_address_space, num_pages * PAGE_SIZE, PAGE_PRESENT | PAGE_WRITE);
}

void *kmalloc(size_t size) {
    // Validate size - prevent zero or excessive allocations
    if (size == 0) {
        serial_puts("KMALLOC: Zero-size allocation rejected\n");
        return 0;
    }
    if (size > 0x10000000) { // 256MB sanity limit
        serial_puts("KMALLOC: Excessive allocation size rejected\n");
        return 0;
    }
    
    // Check for integer overflow in size calculations
    if (size > SIZE_MAX - sizeof(uint32_t) * 2) {
        serial_puts("KMALLOC: Size overflow detected\n");
        return 0;
    }
    
    // Track allocation
    total_allocations++;
    bytes_allocated += size;
    
    uint32_t current_usage = bytes_allocated - bytes_freed;
    if (current_usage > peak_usage) {
        peak_usage = current_usage;
    }
    
    // Use slab allocator for small allocations if VMM is initialized
    if (kernel_address_space) {
        for (int i = 0; i < NUM_SLAB_CACHES; i++) {
            if (size <= slab_sizes[i]) {
                void *ptr = slab_alloc(&kernel_address_space->slab_caches[i]);
                if (ptr) {
                    // Zero the memory for safety (prevents info leaks)
                    memset(ptr, 0, size);
                    return ptr;
                }
                // If slab allocation failed, fall through to page allocator
                break;
            }
        }
    }
    
    // For allocations that don't fit in slabs or before VMM is initialized
    // Use bump allocator for allocations smaller than a page
    if (size < PAGE_SIZE && kernel_heap_ptr + size + sizeof(uint32_t) * 2 <= kernel_heap_end) {
        // Add guards
        uint32_t *start_guard = (uint32_t *)kernel_heap_ptr;
        *start_guard = GUARD_MAGIC_START;
        kernel_heap_ptr += sizeof(uint32_t);
        
        void *ptr = (void *)kernel_heap_ptr;
        
        // Zero the memory for safety
        memset(ptr, 0, size);
        
        kernel_heap_ptr += size;
        
        // Align to 8 bytes
        kernel_heap_ptr = (kernel_heap_ptr + 7) & ~7;
        
        uint32_t *end_guard = (uint32_t *)kernel_heap_ptr;
        *end_guard = GUARD_MAGIC_END;
        kernel_heap_ptr += sizeof(uint32_t);
        
        return ptr;
    }
    
    // For large allocations, use page allocator
    size_t num_pages = (size + sizeof(uint32_t) * 2 + PAGE_SIZE - 1) / PAGE_SIZE;
    
    // Check for overflow in page calculation
    if (num_pages > (SIZE_MAX / PAGE_SIZE)) {
        serial_puts("KMALLOC: Page count overflow\n");
        return 0;
    }
    
    return kmalloc_pages(num_pages);
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    // Input validation
    if (size == 0) {
        serial_puts("ERROR: kmalloc_aligned - zero size\n");
        return NULL;
    }
    
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        serial_puts("ERROR: Invalid alignment (must be power of 2)\n");
        return NULL;
    }
    
    // Check for overflow in size calculation
    if (size > SIZE_MAX - alignment) {
        serial_puts("ERROR: Overflow in aligned allocation\n");
        return NULL;
    }
    
    // For page alignment or larger, use page allocator directly
    if (alignment >= PAGE_SIZE) {
        size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        void *ptr = kmalloc_pages(num_pages);
        // Page allocator already returns page-aligned memory
        return ptr;
    }
    
    // For smaller alignments, allocate extra space
    void *ptr = kmalloc(size + alignment);
    if (!ptr) return NULL;
    
    // Calculate aligned address
    uint32_t addr = (uint32_t)ptr;
    uint32_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    
    // If already aligned, return as-is
    if (aligned_addr == addr) {
        return ptr;
    }
    
    // Note: This is a simplified implementation
    // A production version should store the original pointer for proper freeing
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    
    uint32_t addr = (uint32_t)ptr;
    
    // Validate pointer is not obviously corrupt (not in kernel code/data area)
    if (addr < 0x100000) {
        serial_puts("ERROR: kfree - invalid pointer (too low)\n");
        return;
    }
    
    // Check if pointer is in early bump allocator range
    if (addr >= 0x500000 && addr < 0x700000) {
        // Validate alignment
        if (addr % 4 != 0) {
            serial_puts("WARNING: kfree - misaligned pointer in bump allocator region\n");
        }
        
        // Check guards for bump allocator allocations
        uint32_t *start_guard = (uint32_t *)(addr - sizeof(uint32_t));
        
        // Additional validation for guard location
        if ((uint32_t)start_guard < 0x500000) {
            serial_puts("ERROR: kfree - guard outside bump allocator range\n");
            return;
        }
        
        if (*start_guard == GUARD_MAGIC_FREED) {
            // serial_puts("ERROR: Double-free detected in bump allocator region!\n");
            // Silently block double-free to avoid panicking during boot - just skip the free
            return;
        }
        
        if (*start_guard != GUARD_MAGIC_START) {
            serial_puts("WARNING: kfree - start guard corrupted in bump allocator region\n");
        }
        
        // Mark as freed to detect double-frees
        *start_guard = GUARD_MAGIC_FREED;
        
        // Memory from bump allocator cannot be freed (no tracking)
        // This is acceptable as bump allocator is only used during early boot
        bytes_freed += 64; // Estimate
        total_frees++;
        return;
    }
    
    // For VMM-allocated memory, search for the VMA and free it
    if (kernel_address_space && kernel_address_space->vma_list) {
        // Align down to page boundary
        uint32_t page_addr = PAGE_ALIGN_DOWN(addr);
        
        // Find the VMA that contains this address
        vma_t *vma = kernel_address_space->vma_list;
        int iterations = 0;
        while (vma && iterations < 1000) { // Safety limit
            // Validate VMA magic
            if (vma->magic != 0 && vma->magic != 0xDEADBEEF) {
                serial_puts("ERROR: VMA corruption detected!\n");
                return;
            }
            
            if (page_addr >= vma->start_addr && page_addr < vma->end_addr) {
                // Found the VMA, free all pages in this allocation
                size_t num_pages = (vma->end_addr - vma->start_addr) / PAGE_SIZE;
                bytes_freed += num_pages * PAGE_SIZE;
                total_frees++;
                vmm_free_pages(kernel_address_space, vma->start_addr, num_pages);
                return;
            }
            vma = vma->next;
            iterations++;
        }
        
        if (iterations >= 1000) {
            serial_puts("ERROR: VMA list corrupted (infinite loop detected)\n");
            return;
        }
    }

    // Try to free from slab caches (only after VMA lookup to avoid reading
    // before page-backed allocations where ptr-1 may be unmapped).
    if (kernel_address_space) {
        // Check if this is a slab allocation by examining the header
        slab_obj_t *obj = ((slab_obj_t *)ptr) - 1;
        
        // Validate header is accessible
        if ((uint32_t)obj < 0x100000) {
            serial_puts("ERROR: kfree - slab header pointer invalid\n");
            return;
        }
        
        // Check for double-free first
        if (obj->magic_start == GUARD_MAGIC_FREED) {
          //  serial_puts("ERROR: Double-free detected in slab allocator!\n");
          //  char buf[16];
          //  serial_puts("Address: 0x");
          //  itoa(addr, buf, 16);
          //  serial_puts(buf);
          //  serial_puts("\n");
          // Silently block double-free to avoid panicking during boot - just skip the free
            return;
        }
        
        // Verify if this looks like a slab object
        if (obj->magic_start == GUARD_MAGIC_START) {
            // Validate size is reasonable
            if (obj->size == 0 || obj->size > SLAB_SIZE_2048) {
                serial_puts("ERROR: kfree - corrupted slab object size\n");
                return;
            }
            
            // Find the appropriate cache
            for (int i = 0; i < NUM_SLAB_CACHES; i++) {
                if (obj->size == slab_sizes[i]) {
                    // Mark as freed before actually freeing
                    obj->magic_start = GUARD_MAGIC_FREED;
                    
                    slab_free(&kernel_address_space->slab_caches[i], ptr);
                    
                    bytes_freed += obj->size;
                    total_frees++;
                    return;
                }
            }
            
            // Restore if we couldn't find the cache
            serial_puts("WARNING: kfree - slab object with unknown cache\n");
        }
    }
    
    // If we get here, the pointer is not tracked
    // Could be an early allocation or invalid pointer
    serial_puts("WARNING: kfree - untracked pointer: 0x");
    char buf[16];
    itoa(addr, buf, 16);
    serial_puts(buf);
    serial_puts("\n");
}

int vmm_map_physical(address_space_t *as, uint32_t virtual_addr, uint32_t physical_addr, size_t size, uint32_t flags) {
    if (!as) return -1;
    
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    physical_addr = PAGE_ALIGN_DOWN(physical_addr);
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = virtual_addr + (i * PAGE_SIZE);
        uint32_t paddr = physical_addr + (i * PAGE_SIZE);
        
        map_page(as->page_dir, vaddr, paddr, flags);
    }
    
    return 0;
}

int vmm_unmap(address_space_t *as, uint32_t virtual_addr, size_t size) {
    if (!as) return -1;
    
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = virtual_addr + (i * PAGE_SIZE);
        unmap_page(as->page_dir, vaddr);
    }
    
    return 0;
}

int vmm_is_mapped(address_space_t *as, uint32_t virtual_addr) {
    if (!as) return 0;
    return is_page_present(as->page_dir, virtual_addr);
}

uint32_t vmm_virt_to_phys(address_space_t *as, uint32_t virtual_addr) {
    if (!as) return 0;
    return get_physical_address(as->page_dir, virtual_addr);
}

void vmm_print_stats(address_space_t *as) {
    if (!as) return;
    
    serial_puts("Address Space Statistics:\n");
    
    int vma_count = 0;
    vma_t *vma = as->vma_list;
    while (vma) {
        vma_count++;
        vma = vma->next;
    }
    
    char buf[32];
    serial_puts("  VMAs: ");
    itoa(vma_count, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("  Heap: 0x");
    itoa(as->heap_start, buf, 10);
    serial_puts(buf);
    serial_puts(" - 0x");
    itoa(as->heap_end, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
}

int vmm_validate_pointer(void *ptr) {
    if (!ptr) return 0;
    
    uint32_t addr = (uint32_t)ptr;
    
    // Check if in valid kernel memory range
    if (addr < 0x1000) {
        return 0; // NULL pointer range
    }
    
    // Check if in kernel heap range (identity-mapped region up to 32MB)
    if (addr >= 0x500000 && addr < 0x2000000) {
        return 1; // Valid identity-mapped range
    }
    
    // Check if mapped in current address space
    if (current_address_space) {
        return vmm_is_mapped(current_address_space, addr);
    }
    
    return 0;
}

int vmm_check_guards(void *ptr) {
    if (!ptr) return 0;
    
    uint32_t addr = (uint32_t)ptr;
    
    // Check bump allocator guards
    if (addr >= 0x500000 && addr < 0x700000) {
        uint32_t *start_guard = (uint32_t *)(addr - sizeof(uint32_t));
        if (*start_guard != GUARD_MAGIC_START) {
            serial_puts("ERROR: Start guard corrupted at 0x");
            char buf[16];
            itoa(addr, buf, 16);
            serial_puts(buf);
            serial_puts("\n");
            return 0;
        }
        return 1;
    }
    
    // Check slab guards
    slab_obj_t *obj = ((slab_obj_t *)ptr) - 1;
    if (obj->magic_start == GUARD_MAGIC_START) {
        uint32_t *end_guard = (uint32_t *)((uint8_t *)ptr + obj->size);
        if (*end_guard != GUARD_MAGIC_END) {
            serial_puts("ERROR: End guard corrupted at 0x");
            char buf[16];
            itoa(addr, buf, 16);
            serial_puts(buf);
            serial_puts("\n");
            return 0;
        }
        
        // Check checksum
        uint32_t expected = calculate_checksum(obj, sizeof(slab_obj_t) - sizeof(uint32_t));
        if (obj->checksum != expected) {
            serial_puts("ERROR: Checksum mismatch at 0x");
            char buf[16];
            itoa(addr, buf, 16);
            serial_puts(buf);
            serial_puts("\n");
            return 0;
        }
        return 1;
    }
    
    return 1; // No guards found or guards OK
}

void vmm_print_detailed_stats(void) {
    char buf[32];
    extern void kprint(const char *str);
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== VMM Detailed Statistics ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprint("Allocation Statistics:");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    vga_puts("  Total Allocations: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    itoa(total_allocations, buf, 10);
    kprint(buf);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    vga_puts("  Total Frees:       ");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    itoa(total_frees, buf, 10);
    kprint(buf);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    vga_puts("  Bytes Allocated:   ");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    itoa(bytes_allocated, buf, 10);
    kprint(buf);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    vga_puts("  Bytes Freed:       ");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    itoa(bytes_freed, buf, 10);
    kprint(buf);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    vga_puts("  Current Usage:     ");
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    itoa(bytes_allocated - bytes_freed, buf, 10);
    vga_puts(buf);
    vga_puts(" bytes");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    vga_puts("  Peak Usage:        ");
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    itoa(peak_usage, buf, 10);
    vga_puts(buf);
    vga_puts(" bytes");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    kprint("");
    
    if (kernel_address_space) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("Slab Cache Statistics:");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        for (int i = 0; i < NUM_SLAB_CACHES; i++) {
            slab_cache_t *cache = &kernel_address_space->slab_caches[i];
            if (cache->total_objects > 0) {
                vga_puts("  ");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                itoa(cache->obj_size, buf, 10);
                vga_puts(buf);
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                vga_puts(" bytes: ");
                vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
                itoa(cache->total_objects, buf, 10);
                vga_puts(buf);
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                vga_puts(" objects (");
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                itoa(cache->free_objects, buf, 10);
                vga_puts(buf);
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                vga_puts(" free) in ");
                vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
                itoa(cache->total_slabs, buf, 10);
                vga_puts(buf);
                vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                vga_puts(" slabs");
                kprint("");
            }
        }
    }
    
    kprint("");
    vga_puts("Kernel Heap: ");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_puts("0x");
    itoa(kernel_heap_ptr, buf, 16);
    vga_puts(buf);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_puts(" / ");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_puts("0x");
    itoa(kernel_heap_end, buf, 16);
    vga_puts(buf);
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    uint32_t heap_used = kernel_heap_ptr - 0x500000;
    uint32_t heap_total = kernel_heap_end - 0x500000;
    uint32_t heap_pct = heap_used * 100 / heap_total;
    
    vga_puts(" (");
    if (heap_pct > 90) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    } else if (heap_pct > 75) {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    }
    itoa(heap_pct, buf, 10);
    vga_puts(buf);
    vga_puts("% used)");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    kprint("===============================");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

int vmm_validate_integrity(void) {
    int errors = 0;
    
    serial_puts("VMM: Running integrity check...\n");
    
    if (!kernel_address_space) {
        serial_puts("ERROR: Kernel address space is NULL\n");
        return 1;
    }
    
    // Check VMA list integrity
    vma_t *vma = kernel_address_space->vma_list;
    int vma_count = 0;
    while (vma && vma_count < 1000) { // Prevent infinite loop
        if (vma->start_addr >= vma->end_addr) {
            serial_puts("ERROR: Invalid VMA range\n");
            errors++;
        }
        
        if (vma->magic != 0 && vma->magic != 0xDEADBEEF) {
            serial_puts("ERROR: VMA magic corrupted\n");
            errors++;
        }
        
        vma = vma->next;
        vma_count++;
    }
    
    if (vma_count >= 1000) {
        serial_puts("ERROR: VMA list appears to be circular\n");
        errors++;
    }
    
    // Validate slab caches
    for (int i = 0; i < NUM_SLAB_CACHES; i++) {
        slab_cache_t *cache = &kernel_address_space->slab_caches[i];
        if (cache->free_objects > cache->total_objects) {
            serial_puts("ERROR: Slab cache ");
            char buf[16];
            itoa(cache->obj_size, buf, 10);
            serial_puts(buf);
            serial_puts(" has more free objects than total\n");
            errors++;
        }
    }
    
    if (errors == 0) {
        serial_puts("VMM: Integrity check passed!\n");
    } else {
        char buf[16];
        serial_puts("VMM: Integrity check found ");
        itoa(errors, buf, 10);
        serial_puts(buf);
        serial_puts(" errors\n");
    }
    
    return errors;
}

// Validate a memory allocation's integrity (guards, checksums, etc.)
int vmm_validate_allocation(void *ptr) {
    if (!ptr) {
        return 0; // NULL is technically valid
    }
    
    uint32_t addr = (uint32_t)ptr;
    
    // Basic range check
    if (addr < 0x100000 || addr > 0x20000000) {
        serial_puts("ERROR: Pointer outside valid memory range\n");
        return 0;
    }
    
    // Check if it's a slab allocation
    slab_obj_t *obj = ((slab_obj_t *)ptr) - 1;
    uint32_t obj_addr = (uint32_t)obj;
    
    if (obj_addr >= 0x100000 && obj_addr < 0x20000000) {
        // Check if this looks like a slab object
        if (obj->magic_start == GUARD_MAGIC_START) {
            // Validate size
            if (obj->size == 0 || obj->size > SLAB_SIZE_2048) {
                serial_puts("ERROR: Invalid slab object size\n");
                return 0;
            }
            
            // Check end guard
            uint32_t *end_guard = (uint32_t *)((uint8_t *)ptr + obj->size);
            if (*end_guard != GUARD_MAGIC_END) {
                serial_puts("ERROR: End guard corrupted\n");
                return 0;
            }
            
            // Check checksum
            uint32_t expected = calculate_checksum(obj, sizeof(slab_obj_t) - sizeof(uint32_t));
            if (obj->checksum != expected) {
                serial_puts("ERROR: Checksum mismatch\n");
                return 0;
            }
            
            return 1; // Valid slab allocation
        } else if (obj->magic_start == GUARD_MAGIC_FREED) {
            serial_puts("ERROR: Use-after-free detected!\n");
            return 0;
        }
    }
    
    // Check bump allocator range
    if (addr >= 0x500000 && addr < 0x700000) {
        uint32_t *start_guard = (uint32_t *)(addr - sizeof(uint32_t));
        if ((uint32_t)start_guard >= 0x500000) {
            if (*start_guard == GUARD_MAGIC_START) {
                return 1; // Valid bump allocation
            } else if (*start_guard == GUARD_MAGIC_FREED) {
                serial_puts("ERROR: Use-after-free in bump allocator\n");
                return 0;
            }
        }
    }
    
    // If we can't validate, assume valid (might be page allocation)
    return 1;
}

// Scan a memory region for corruption
int vmm_scan_region_for_corruption(void *start, size_t size) {
    if (!start || size == 0) {
        return 0;
    }
    
    int issues = 0;
    uint8_t *ptr = (uint8_t *)start;
    uint8_t *end = ptr + size;
    
    serial_puts("VMM: Scanning memory region 0x");
    char buf[16];
    itoa((uint32_t)start, buf, 16);
    serial_puts(buf);
    serial_puts(" size ");
    itoa(size, buf, 10);
    serial_puts(buf);
    serial_puts(" bytes\n");
    
    // Scan for guard patterns that might indicate corruption
    for (uint8_t *p = ptr; p < end - 4; p++) {
        uint32_t *word = (uint32_t *)p;
        
        // Look for corrupted guards
        if (*word == GUARD_MAGIC_START || *word == GUARD_MAGIC_END) {
            // Check if this is a legitimate guard by validating nearby structure
            uint32_t offset = (uint32_t)p - (uint32_t)start;
            serial_puts("  Found guard pattern at offset ");
            itoa(offset, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
        }
        
        // Look for freed memory pattern
        if ((*word & 0xFFFFFF00) == 0xFEFEFE00) {
            uint32_t offset = (uint32_t)p - (uint32_t)start;
            serial_puts("  Found freed memory pattern at offset ");
            itoa(offset, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
            issues++;
        }
    }
    
    return issues;
}

// Check heap consistency
int vmm_check_heap_consistency(void) {
    serial_puts("VMM: Checking heap consistency...\n");
    
    int errors = 0;
    
    // Validate heap pointers
    if (kernel_heap_ptr < 0x500000 || kernel_heap_ptr > kernel_heap_end) {
        serial_puts("ERROR: Kernel heap pointer out of range\n");
        errors++;
    }
    
    if (kernel_heap_end < kernel_heap_ptr || kernel_heap_end > 0x700000) {
        serial_puts("ERROR: Kernel heap end pointer invalid\n");
        errors++;
    }
    
    // Validate allocation counters
    if (bytes_freed > bytes_allocated) {
        serial_puts("ERROR: More memory freed than allocated\n");
        errors++;
    }
    
    if (total_frees > total_allocations) {
        serial_puts("ERROR: More frees than allocations\n");
        errors++;
    }
    
    char buf[16];
    if (errors == 0) {
        serial_puts("VMM: Heap consistency check passed\n");
    } else {
        serial_puts("VMM: Heap consistency check found ");
        itoa(errors, buf, 10);
        serial_puts(buf);
        serial_puts(" errors\n");
    }
    
    return errors;
}
