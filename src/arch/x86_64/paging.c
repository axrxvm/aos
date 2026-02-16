/*
 * === AOS HEADER BEGIN ===
 * src/arch/x86_64/paging.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <arch/x86_64/paging.h>
#include <arch/x86_64/isr.h>
#include <serial.h>
#include <pmm.h>
#include <stdlib.h>
#include <string.h>
#include <panic.h>

page_directory_t* current_directory = 0;
page_directory_t* kernel_directory = 0;

static page_directory_t kernel_directory_static;

/* Canonical 4-level page-table constants for x86_64 */
#define X86_64_INDEX_MASK        0x1FFULL
#define X86_64_FLAGS_MASK        0xFFFULL
#define X86_64_ADDR_MASK         0x000FFFFFFFFFF000ULL
#define X86_64_2MB_ADDR_MASK     0x000FFFFFFFE00000ULL
#define X86_64_1GB_ADDR_MASK     0x000FFFFFC0000000ULL
#define X86_64_NX_BIT            (1ULL << 63)

static inline uint16_t pml4_index(uint64_t va) { return (uint16_t)((va >> 39) & X86_64_INDEX_MASK); }
static inline uint16_t pdpt_index(uint64_t va) { return (uint16_t)((va >> 30) & X86_64_INDEX_MASK); }
static inline uint16_t pd_index(uint64_t va)   { return (uint16_t)((va >> 21) & X86_64_INDEX_MASK); }
static inline uint16_t pt_index(uint64_t va)   { return (uint16_t)((va >> 12) & X86_64_INDEX_MASK); }

static inline int entry_present(uint64_t entry) {
    return (entry & PAGE_PRESENT) != 0;
}

static inline int entry_large(uint64_t entry) {
    return (entry & PAGE_SIZE_FLAG) != 0;
}

static inline uint64_t entry_addr(uint64_t entry) {
    return entry & X86_64_ADDR_MASK;
}

static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void write_cr3(uint64_t cr3) {
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

static uint64_t* alloc_table_page(void) {
    void* page = alloc_page_from_zone(PMM_ZONE_DMA);
    if (!page) {
        page = alloc_page();
        if (!page) {
            return 0;
        }
    }

    memset(page, 0, PAGE_SIZE);
    return (uint64_t*)page;
}

static uint64_t normalize_entry_flags(uint32_t flags) {
    uint64_t entry_flags = (uint64_t)(flags & (PAGE_PRESENT | PAGE_WRITE | PAGE_USER |
                                               PAGE_WRITETHROUGH | PAGE_NOCACHE |
                                               PAGE_ACCESSED | PAGE_DIRTY | PAGE_GLOBAL));
    entry_flags |= PAGE_PRESENT;
    return entry_flags;
}

static int split_2mb_page(uint64_t* pd, uint16_t pd_i) {
    uint64_t pde = pd[pd_i];
    if (!entry_present(pde) || !entry_large(pde)) {
        return 0;
    }

    uint64_t* pt = alloc_table_page();
    if (!pt) {
        serial_puts("x86_64 paging: failed to allocate PT while splitting 2MB page\n");
        return -1;
    }

    uint64_t base_phys = pde & X86_64_2MB_ADDR_MASK;
    uint64_t inherited_flags = (pde & (X86_64_FLAGS_MASK | X86_64_NX_BIT)) & ~((uint64_t)PAGE_SIZE_FLAG);

    for (uint16_t i = 0; i < 512; i++) {
        uint64_t phys = base_phys + ((uint64_t)i * PAGE_SIZE);
        pt[i] = (phys & X86_64_ADDR_MASK) | inherited_flags;
    }

    pd[pd_i] = ((uint64_t)(uintptr_t)pt & X86_64_ADDR_MASK) | inherited_flags;
    return 0;
}

static int clone_kernel_mappings(page_directory_t* dir) {
    if (!kernel_directory || !kernel_directory->pml4) {
        return 0;
    }

    for (uint16_t pml4_i = 0; pml4_i < 512; pml4_i++) {
        uint64_t pml4e = kernel_directory->pml4[pml4_i];
        if (!entry_present(pml4e)) {
            continue;
        }

        uint64_t* src_pdpt = (uint64_t*)(uintptr_t)entry_addr(pml4e);
        uint64_t* dst_pdpt = alloc_table_page();
        if (!dst_pdpt) {
            return -1;
        }

        uint64_t pml4_flags = (pml4e & (X86_64_FLAGS_MASK | X86_64_NX_BIT)) & ~((uint64_t)PAGE_USER);
        dir->pml4[pml4_i] = ((uint64_t)(uintptr_t)dst_pdpt & X86_64_ADDR_MASK) | pml4_flags;

        for (uint16_t pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            uint64_t pdpte = src_pdpt[pdpt_i];
            if (!entry_present(pdpte)) {
                continue;
            }

            if (entry_large(pdpte)) {
                /* 1GiB page: copy mapping metadata, force supervisor by default. */
                dst_pdpt[pdpt_i] = pdpte & ~((uint64_t)PAGE_USER);
                continue;
            }

            uint64_t* src_pd = (uint64_t*)(uintptr_t)entry_addr(pdpte);
            uint64_t* dst_pd = alloc_table_page();
            if (!dst_pd) {
                return -1;
            }

            uint64_t pdpt_flags = (pdpte & (X86_64_FLAGS_MASK | X86_64_NX_BIT)) & ~((uint64_t)PAGE_USER);
            dst_pdpt[pdpt_i] = ((uint64_t)(uintptr_t)dst_pd & X86_64_ADDR_MASK) | pdpt_flags;

            for (uint16_t pd_i = 0; pd_i < 512; pd_i++) {
                uint64_t pde = src_pd[pd_i];
                if (!entry_present(pde)) {
                    continue;
                }

                if (entry_large(pde)) {
                    /* 2MiB page from boot identity mapping */
                    dst_pd[pd_i] = pde & ~((uint64_t)PAGE_USER);
                    continue;
                }

                uint64_t* src_pt = (uint64_t*)(uintptr_t)entry_addr(pde);
                uint64_t* dst_pt = alloc_table_page();
                if (!dst_pt) {
                    return -1;
                }

                uint64_t pd_flags = (pde & (X86_64_FLAGS_MASK | X86_64_NX_BIT)) & ~((uint64_t)PAGE_USER);
                dst_pd[pd_i] = ((uint64_t)(uintptr_t)dst_pt & X86_64_ADDR_MASK) | pd_flags;

                memcpy(dst_pt, src_pt, PAGE_SIZE);
                for (uint16_t pt_i = 0; pt_i < 512; pt_i++) {
                    dst_pt[pt_i] &= ~((uint64_t)PAGE_USER);
                }
            }
        }
    }

    return 0;
}

void init_paging(void) {
    uint64_t cr3 = read_cr3() & X86_64_ADDR_MASK;

    kernel_directory_static.pml4 = (uint64_t*)(uintptr_t)cr3;
    kernel_directory_static.physical_addr = cr3;
    kernel_directory_static.owns_tables = 0;

    kernel_directory = &kernel_directory_static;
    current_directory = kernel_directory;

    register_interrupt_handler(14, page_fault_handler);

    serial_puts("Paging initialized (x86_64, 4-level page tables).\n");
}

page_directory_t* create_page_directory(void) {
    page_directory_t* dir = (page_directory_t*)alloc_table_page();
    if (!dir) {
        return 0;
    }

    dir->pml4 = alloc_table_page();
    if (!dir->pml4) {
        free_page(dir);
        return 0;
    }

    dir->physical_addr = (uint64_t)(uintptr_t)dir->pml4;
    dir->owns_tables = 1;

    if (clone_kernel_mappings(dir) != 0) {
        destroy_page_directory(dir);
        return 0;
    }

    return dir;
}

void destroy_page_directory(page_directory_t* dir) {
    if (!dir || dir == kernel_directory) {
        return;
    }

    if (dir->owns_tables && dir->pml4) {
        for (uint16_t pml4_i = 0; pml4_i < 512; pml4_i++) {
            uint64_t pml4e = dir->pml4[pml4_i];
            if (!entry_present(pml4e)) {
                continue;
            }

            uint64_t* pdpt = (uint64_t*)(uintptr_t)entry_addr(pml4e);
            if (!pdpt) {
                continue;
            }

            for (uint16_t pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
                uint64_t pdpte = pdpt[pdpt_i];
                if (!entry_present(pdpte) || entry_large(pdpte)) {
                    continue;
                }

                uint64_t* pd = (uint64_t*)(uintptr_t)entry_addr(pdpte);
                if (!pd) {
                    continue;
                }

                for (uint16_t pd_i = 0; pd_i < 512; pd_i++) {
                    uint64_t pde = pd[pd_i];
                    if (!entry_present(pde) || entry_large(pde)) {
                        continue;
                    }

                    uint64_t* pt = (uint64_t*)(uintptr_t)entry_addr(pde);
                    if (pt) {
                        free_page(pt);
                    }
                }

                free_page(pd);
            }

            free_page(pdpt);
        }

        free_page(dir->pml4);
    }

    free_page(dir);
}

void switch_page_directory(page_directory_t* dir) {
    if (!dir || !dir->physical_addr) {
        return;
    }
    current_directory = dir;
    write_cr3(dir->physical_addr);
}

void map_page(page_directory_t* dir, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    if (!dir || !dir->pml4) {
        return;
    }

    uint64_t va = (uint64_t)PAGE_ALIGN_DOWN(virtual_addr);
    uint64_t pa = (uint64_t)PAGE_ALIGN_DOWN(physical_addr);
    uint64_t entry_flags = normalize_entry_flags(flags);

    uint16_t pml4_i = pml4_index(va);
    uint16_t pdpt_i = pdpt_index(va);
    uint16_t pd_i = pd_index(va);
    uint16_t pt_i = pt_index(va);

    uint64_t pml4e = dir->pml4[pml4_i];
    uint64_t* pdpt;
    if (!entry_present(pml4e)) {
        pdpt = alloc_table_page();
        if (!pdpt) {
            return;
        }
        dir->pml4[pml4_i] = ((uint64_t)(uintptr_t)pdpt & X86_64_ADDR_MASK) |
                            PAGE_PRESENT | PAGE_WRITE | (entry_flags & PAGE_USER);
    } else {
        if (entry_flags & PAGE_USER) dir->pml4[pml4_i] |= PAGE_USER;
        if (entry_flags & PAGE_WRITE) dir->pml4[pml4_i] |= PAGE_WRITE;
        pdpt = (uint64_t*)(uintptr_t)entry_addr(dir->pml4[pml4_i]);
    }

    uint64_t pdpte = pdpt[pdpt_i];
    uint64_t* pd;
    if (!entry_present(pdpte)) {
        pd = alloc_table_page();
        if (!pd) {
            return;
        }
        pdpt[pdpt_i] = ((uint64_t)(uintptr_t)pd & X86_64_ADDR_MASK) |
                       PAGE_PRESENT | PAGE_WRITE | (entry_flags & PAGE_USER);
    } else {
        if (entry_large(pdpte)) {
            serial_puts("x86_64 paging: 1GiB page split not supported\n");
            return;
        }
        if (entry_flags & PAGE_USER) pdpt[pdpt_i] |= PAGE_USER;
        if (entry_flags & PAGE_WRITE) pdpt[pdpt_i] |= PAGE_WRITE;
        pd = (uint64_t*)(uintptr_t)entry_addr(pdpt[pdpt_i]);
    }

    uint64_t pde = pd[pd_i];
    uint64_t* pt;
    if (!entry_present(pde)) {
        pt = alloc_table_page();
        if (!pt) {
            return;
        }
        pd[pd_i] = ((uint64_t)(uintptr_t)pt & X86_64_ADDR_MASK) |
                   PAGE_PRESENT | PAGE_WRITE | (entry_flags & PAGE_USER);
    } else {
        if (entry_large(pde)) {
            if (split_2mb_page(pd, pd_i) != 0) {
                return;
            }
        }
        if (entry_flags & PAGE_USER) pd[pd_i] |= PAGE_USER;
        if (entry_flags & PAGE_WRITE) pd[pd_i] |= PAGE_WRITE;
        pt = (uint64_t*)(uintptr_t)entry_addr(pd[pd_i]);
    }

    pt[pt_i] = (pa & X86_64_ADDR_MASK) | entry_flags;

    if (dir == current_directory) {
        flush_tlb_single((uint32_t)va);
    }
}

void unmap_page(page_directory_t* dir, uint32_t virtual_addr) {
    if (!dir || !dir->pml4) {
        return;
    }

    uint64_t va = (uint64_t)PAGE_ALIGN_DOWN(virtual_addr);
    uint16_t pml4_i = pml4_index(va);
    uint16_t pdpt_i = pdpt_index(va);
    uint16_t pd_i = pd_index(va);
    uint16_t pt_i = pt_index(va);

    uint64_t pml4e = dir->pml4[pml4_i];
    if (!entry_present(pml4e)) {
        return;
    }

    uint64_t* pdpt = (uint64_t*)(uintptr_t)entry_addr(pml4e);
    uint64_t pdpte = pdpt[pdpt_i];
    if (!entry_present(pdpte) || entry_large(pdpte)) {
        return;
    }

    uint64_t* pd = (uint64_t*)(uintptr_t)entry_addr(pdpte);
    uint64_t pde = pd[pd_i];
    if (!entry_present(pde)) {
        return;
    }

    if (entry_large(pde)) {
        if (split_2mb_page(pd, pd_i) != 0) {
            return;
        }
    }

    uint64_t* pt = (uint64_t*)(uintptr_t)entry_addr(pd[pd_i]);
    pt[pt_i] = 0;

    if (dir == current_directory) {
        flush_tlb_single((uint32_t)va);
    }
}

uint32_t get_physical_address(page_directory_t* dir, uint32_t virtual_addr) {
    if (!dir || !dir->pml4) {
        return 0;
    }

    uint64_t va = (uint64_t)virtual_addr;
    uint16_t pml4_i = pml4_index(va);
    uint16_t pdpt_i = pdpt_index(va);
    uint16_t pd_i = pd_index(va);
    uint16_t pt_i = pt_index(va);

    uint64_t pml4e = dir->pml4[pml4_i];
    if (!entry_present(pml4e)) {
        return 0;
    }

    uint64_t* pdpt = (uint64_t*)(uintptr_t)entry_addr(pml4e);
    uint64_t pdpte = pdpt[pdpt_i];
    if (!entry_present(pdpte)) {
        return 0;
    }

    if (entry_large(pdpte)) {
        uint64_t phys = (pdpte & X86_64_1GB_ADDR_MASK) | (va & 0x3FFFFFFFULL);
        return (uint32_t)phys;
    }

    uint64_t* pd = (uint64_t*)(uintptr_t)entry_addr(pdpte);
    uint64_t pde = pd[pd_i];
    if (!entry_present(pde)) {
        return 0;
    }

    if (entry_large(pde)) {
        uint64_t phys = (pde & X86_64_2MB_ADDR_MASK) | (va & 0x1FFFFFULL);
        return (uint32_t)phys;
    }

    uint64_t* pt = (uint64_t*)(uintptr_t)entry_addr(pde);
    uint64_t pte = pt[pt_i];
    if (!entry_present(pte)) {
        return 0;
    }

    return (uint32_t)((pte & X86_64_ADDR_MASK) | (va & 0xFFFULL));
}

int is_page_present(page_directory_t* dir, uint32_t virtual_addr) {
    if (!dir || !dir->pml4) {
        return 0;
    }

    uint64_t va = (uint64_t)virtual_addr;
    uint16_t pml4_i = pml4_index(va);
    uint16_t pdpt_i = pdpt_index(va);
    uint16_t pd_i = pd_index(va);
    uint16_t pt_i = pt_index(va);

    uint64_t pml4e = dir->pml4[pml4_i];
    if (!entry_present(pml4e)) {
        return 0;
    }

    uint64_t* pdpt = (uint64_t*)(uintptr_t)entry_addr(pml4e);
    uint64_t pdpte = pdpt[pdpt_i];
    if (!entry_present(pdpte)) {
        return 0;
    }

    if (entry_large(pdpte)) {
        return 1;
    }

    uint64_t* pd = (uint64_t*)(uintptr_t)entry_addr(pdpte);
    uint64_t pde = pd[pd_i];
    if (!entry_present(pde)) {
        return 0;
    }

    if (entry_large(pde)) {
        return 1;
    }

    uint64_t* pt = (uint64_t*)(uintptr_t)entry_addr(pde);
    return entry_present(pt[pt_i]);
}

void page_fault_handler(registers_t* regs) {
    uint64_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r"(faulting_address));

    serial_puts("\n=== PAGE FAULT (x86_64) ===\n");
    serial_puts("Fault address: 0x");
    char buf[32];
    itoa((uint32_t)faulting_address, buf, 16);
    serial_puts(buf);
    serial_puts("\n");

    panic_screen(regs, "x86_64 page fault", __FILE__, __LINE__);
}

void flush_tlb_single(uint32_t virtual_addr) {
    asm volatile("invlpg (%0)" :: "r"((uint64_t)virtual_addr) : "memory");
}

void flush_tlb_full(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

void remap_vga_buffer(void) {
    if (!kernel_directory) {
        return;
    }
    map_page(kernel_directory, 0xB8000, 0xB8000, PAGE_PRESENT | PAGE_WRITE);
    flush_tlb_single(0xB8000);
}

void enable_paging(uint32_t page_directory_physical) {
    write_cr3((uint64_t)page_directory_physical);
}

void identity_map_range(page_directory_t* dir, uint32_t start, uint32_t end, uint32_t flags) {
    uint32_t map_flags = flags | PAGE_PRESENT;
    start = PAGE_ALIGN_DOWN(start);
    end = PAGE_ALIGN_UP(end);

    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        map_page(dir, addr, addr, map_flags);
    }
}
