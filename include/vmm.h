/*
 * === AOS HEADER BEGIN ===
 * include/vmm.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include <arch/i386/paging.h>

// Virtual Memory Manager - handles higher-level memory operations

// Memory regions for different purposes
#define VMM_USER_STACK_TOP      0xBFFFFFFF  // User stack grows down from here
#define VMM_USER_HEAP_START     0x10000000  // User heap starts at 256MB
#define VMM_USER_CODE_START     0x08048000  // Traditional ELF load address
#define VMM_KERNEL_HEAP_START   KERNEL_HEAP_START

// Allocation flags
#define VMM_PRESENT     PAGE_PRESENT
#define VMM_WRITE       PAGE_WRITE
#define VMM_USER        PAGE_USER
#define VMM_NOCACHE     PAGE_NOCACHE

// Virtual memory area structure (for tracking allocations)
typedef struct vma {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t flags;
    struct vma *next;
} vma_t;

// Address space structure (per-process)
typedef struct {
    page_directory_t *page_dir;
    vma_t *vma_list;
    uint32_t heap_start;
    uint32_t heap_end;
    uint32_t stack_top;
} address_space_t;

// Initialize VMM
void init_vmm(void);

// Address space management
address_space_t *create_address_space(void);
void destroy_address_space(address_space_t *as);
void switch_address_space(address_space_t *as);

// Memory allocation functions
void *vmm_alloc_pages(address_space_t *as, uint32_t virtual_addr, size_t num_pages, uint32_t flags);
void *vmm_alloc_at(address_space_t *as, uint32_t virtual_addr, size_t size, uint32_t flags);
void *vmm_alloc_anywhere(address_space_t *as, size_t size, uint32_t flags);
void vmm_free_pages(address_space_t *as, uint32_t virtual_addr, size_t num_pages);

// Kernel memory allocation (using kernel address space)
void *kmalloc_pages(size_t num_pages);
void *kmalloc(size_t size);
void kfree(void *ptr);

// Memory mapping
int vmm_map_physical(address_space_t *as, uint32_t virtual_addr, uint32_t physical_addr, size_t size, uint32_t flags);
int vmm_unmap(address_space_t *as, uint32_t virtual_addr, size_t size);

// Utility functions
int vmm_is_mapped(address_space_t *as, uint32_t virtual_addr);
uint32_t vmm_virt_to_phys(address_space_t *as, uint32_t virtual_addr);
void vmm_print_stats(address_space_t *as);

// Current address space
extern address_space_t *current_address_space;
extern address_space_t *kernel_address_space;

#endif // VMM_H
