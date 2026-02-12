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
#include <stdlib.h>
#include <string.h>
#include <panic.h>

// Current address spaces
address_space_t *current_address_space = 0;
address_space_t *kernel_address_space = 0;

// Simple kernel heap allocator (before we have a proper heap)
static uint32_t kernel_heap_ptr = 0x500000;  // Start at 5MB
static uint32_t kernel_heap_end = 0x700000;  // End at 7MB

void init_vmm(void) {
    serial_puts("Initializing Virtual Memory Manager...\n");
    
    // Create kernel address space structure
    kernel_address_space = (address_space_t *)kmalloc(sizeof(address_space_t));
    if (!kernel_address_space) {
        panic("Failed to create kernel address space");
    }
    
    // Clear structure
    memset(kernel_address_space, 0, sizeof(address_space_t));
    
    // Use existing kernel page directory
    kernel_address_space->page_dir = kernel_directory;
    kernel_address_space->heap_start = kernel_heap_ptr;
    kernel_address_space->heap_end = kernel_heap_ptr;
    kernel_address_space->stack_top = 0; // Kernel doesn't use user stack
    
    current_address_space = kernel_address_space;
    
    serial_puts("VMM initialized successfully!\n");
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
    // Identity mapped kernel (0-8MB)
    for (uint32_t addr = 0; addr < 0x800000; addr += PAGE_SIZE) {
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
        return 0;
    }
    if (size > 0x10000000) { // 256MB sanity limit
        serial_puts("KMALLOC: Excessive allocation size rejected\n");
        return 0;
    }
    
    // For small allocations, always use bump allocator (identity-mapped region)
    // Higher-half heap (0xC1000000+) is not yet mapped
    if (size < PAGE_SIZE) {
        if (kernel_heap_ptr + size > kernel_heap_end) {
            return 0; // Out of memory
        }
        
        void *ptr = (void *)kernel_heap_ptr;
        kernel_heap_ptr += (size + 7) & ~7; // 8-byte align
        return ptr;
    }
    
    // For large allocations, use page allocator
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    return kmalloc_pages(num_pages);
}

void kfree(void *ptr) {
    if (!ptr) return;
    
    // Check if pointer is in early bump allocator range
    uint32_t addr = (uint32_t)ptr;
    if (addr >= 0x500000 && addr < 0x700000) {
        // Memory from bump allocator cannot be freed (no tracking)
        // This is acceptable as bump allocator is only used during early boot
        return;
    }
    
    // For VMM-allocated memory, search for the VMA and free it
    if (kernel_address_space && kernel_address_space->vma_list) {
        // Align down to page boundary
        uint32_t page_addr = PAGE_ALIGN_DOWN(addr);
        
        // Find the VMA that contains this address
        vma_t *vma = kernel_address_space->vma_list;
        while (vma) {
            if (page_addr >= vma->start_addr && page_addr < vma->end_addr) {
                // Found the VMA, free all pages in this allocation
                size_t num_pages = (vma->end_addr - vma->start_addr) / PAGE_SIZE;
                vmm_free_pages(kernel_address_space, vma->start_addr, num_pages);
                return;
            }
            vma = vma->next;
        }
    }
    
    // If we get here, the pointer is not tracked (possible early allocation or invalid)
    // Safe to ignore as we can't free what we don't track
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
