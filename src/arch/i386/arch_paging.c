/*
 * === AOS HEADER BEGIN ===
 * src/arch/i386/arch_paging.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <arch_paging.h>
#include <arch/paging.h>
#include <arch/isr.h>
#include <memory.h>
#include <vmm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Map architecture-independent page directory to i386 implementation
struct arch_page_directory {
    page_directory_t* i386_dir;
};

// Initialize paging subsystem
void arch_paging_init(void) {
    init_paging();
}

// Create a new page directory
arch_page_directory_t* arch_paging_create_directory(void) {
    arch_page_directory_t* dir = (arch_page_directory_t*)kmalloc(sizeof(arch_page_directory_t));
    if (!dir) return NULL;
    
    dir->i386_dir = create_page_directory();
    if (!dir->i386_dir) {
        kfree(dir);
        return NULL;
    }
    
    return dir;
}

// Destroy a page directory
void arch_paging_destroy_directory(arch_page_directory_t* dir) {
    if (dir && dir->i386_dir) {
        destroy_page_directory(dir->i386_dir);
        kfree(dir);
    }
}

// Switch to a page directory
void arch_paging_switch_directory(arch_page_directory_t* dir) {
    if (dir && dir->i386_dir) {
        switch_page_directory(dir->i386_dir);
    }
}

// Get current page directory
arch_page_directory_t* arch_paging_get_current_directory(void) {
    static arch_page_directory_t current_wrapper;
    current_wrapper.i386_dir = current_directory;
    return &current_wrapper;
}

// Map flags conversion helper
static uint32_t convert_flags_to_i386(uint32_t arch_flags) {
    uint32_t i386_flags = 0;
    
    if (arch_flags & ARCH_PAGE_PRESENT)  i386_flags |= PAGE_PRESENT;
    if (arch_flags & ARCH_PAGE_WRITABLE) i386_flags |= PAGE_WRITE;
    if (arch_flags & ARCH_PAGE_USER)     i386_flags |= PAGE_USER;
    if (arch_flags & ARCH_PAGE_NOCACHE)  i386_flags |= PAGE_NOCACHE;
    
    return i386_flags;
}

// Map a virtual address to a physical address
bool arch_paging_map(arch_page_directory_t* dir, uintptr_t virt, uintptr_t phys, uint32_t flags) {
    if (!dir || !dir->i386_dir) return false;
    
    uint32_t i386_flags = convert_flags_to_i386(flags);
    map_page(dir->i386_dir, (uint32_t)virt, (uint32_t)phys, i386_flags);
    return true;
}

// Unmap a virtual address
bool arch_paging_unmap(arch_page_directory_t* dir, uintptr_t virt) {
    if (!dir || !dir->i386_dir) return false;
    
    unmap_page(dir->i386_dir, (uint32_t)virt);
    return true;
}

// Get physical address from virtual address
uintptr_t arch_paging_get_physical(arch_page_directory_t* dir, uintptr_t virt) {
    if (!dir || !dir->i386_dir) return 0;
    
    return (uintptr_t)get_physical_address(dir->i386_dir, (uint32_t)virt);
}

// Identity map a range
bool arch_paging_identity_map_range(arch_page_directory_t* dir, uintptr_t start, uintptr_t end, uint32_t flags) {
    if (!dir || !dir->i386_dir) return false;
    
    uint32_t i386_flags = convert_flags_to_i386(flags);
    identity_map_range(dir->i386_dir, (uint32_t)start, (uint32_t)end, i386_flags);
    return true;
}

// Page fault handler wrapper
void arch_paging_fault_handler(void* regs) {
    // Call i386-specific page fault handler
    page_fault_handler((registers_t*)regs);
}
