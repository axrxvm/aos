/*
 * === AOS HEADER BEGIN ===
 * src/arch/x86_64/arch_paging.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <arch_paging.h>
#include <arch/x86_64/paging.h>
#include <vmm.h>
#include <stdbool.h>
#include <stdint.h>

struct arch_page_directory {
    page_directory_t* x86_64_dir;
};

void arch_paging_init(void) {
    init_paging();
}

arch_page_directory_t* arch_paging_create_directory(void) {
    arch_page_directory_t* dir = (arch_page_directory_t*)kmalloc(sizeof(arch_page_directory_t));
    if (!dir) return NULL;

    dir->x86_64_dir = create_page_directory();
    if (!dir->x86_64_dir) {
        kfree(dir);
        return NULL;
    }

    return dir;
}

void arch_paging_destroy_directory(arch_page_directory_t* dir) {
    if (dir && dir->x86_64_dir) {
        destroy_page_directory(dir->x86_64_dir);
        kfree(dir);
    }
}

void arch_paging_switch_directory(arch_page_directory_t* dir) {
    if (dir && dir->x86_64_dir) {
        switch_page_directory(dir->x86_64_dir);
    }
}

arch_page_directory_t* arch_paging_get_current_directory(void) {
    static arch_page_directory_t current_wrapper;
    current_wrapper.x86_64_dir = current_directory;
    return &current_wrapper;
}

static uint32_t convert_flags_to_x86(uint32_t arch_flags) {
    uint32_t x86_flags = 0;

    if (arch_flags & ARCH_PAGE_PRESENT)  x86_flags |= PAGE_PRESENT;
    if (arch_flags & ARCH_PAGE_WRITABLE) x86_flags |= PAGE_WRITE;
    if (arch_flags & ARCH_PAGE_USER)     x86_flags |= PAGE_USER;
    if (arch_flags & ARCH_PAGE_NOCACHE)  x86_flags |= PAGE_NOCACHE;

    return x86_flags;
}

bool arch_paging_map(arch_page_directory_t* dir, uintptr_t virt, uintptr_t phys, uint32_t flags) {
    if (!dir || !dir->x86_64_dir) return false;

    uint32_t x86_flags = convert_flags_to_x86(flags);
    map_page(dir->x86_64_dir, virt, phys, x86_flags);
    return true;
}

bool arch_paging_unmap(arch_page_directory_t* dir, uintptr_t virt) {
    if (!dir || !dir->x86_64_dir) return false;

    unmap_page(dir->x86_64_dir, virt);
    return true;
}

uintptr_t arch_paging_get_physical(arch_page_directory_t* dir, uintptr_t virt) {
    if (!dir || !dir->x86_64_dir) return 0;

    return get_physical_address(dir->x86_64_dir, virt);
}

bool arch_paging_identity_map_range(arch_page_directory_t* dir, uintptr_t start, uintptr_t end, uint32_t flags) {
    if (!dir || !dir->x86_64_dir) return false;

    uint32_t x86_flags = convert_flags_to_x86(flags);
    identity_map_range(dir->x86_64_dir, start, end, x86_flags);
    return true;
}

void arch_paging_fault_handler(void* regs) {
    page_fault_handler((registers_t*)regs);
}
