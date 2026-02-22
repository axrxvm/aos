/*
 * === AOS HEADER BEGIN ===
 * include/arch/x86_64/paging.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_X86_64_PAGING_H
#define ARCH_X86_64_PAGING_H

#include <stdint.h>
#include <arch/x86_64/isr.h>

#define PAGE_SIZE 4096
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define KERNEL_VIRTUAL_BASE UINT64_C(0x00000000C0000000)
#define USER_VIRTUAL_BASE   UINT64_C(0x0000000000000000)
#define KERNEL_HEAP_START   UINT64_C(0x00000000C1000000)

#define PAGE_PRESENT        0x001
#define PAGE_WRITE          0x002
#define PAGE_USER           0x004
#define PAGE_WRITETHROUGH   0x008
#define PAGE_NOCACHE        0x010
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040
#define PAGE_SIZE_FLAG      0x080
#define PAGE_GLOBAL         0x100
#define PAGE_NOEXEC         (1ULL << 63)

typedef struct {
    uint64_t* pml4;
    uint64_t physical_addr;
    uint8_t owns_tables;
} page_directory_t;

extern page_directory_t* current_directory;
extern page_directory_t* kernel_directory;

void init_paging(void);
void switch_page_directory(page_directory_t* dir);
page_directory_t* create_page_directory(void);
void destroy_page_directory(page_directory_t* dir);

void map_page(page_directory_t* dir, uintptr_t virtual_addr, uintptr_t physical_addr, uint32_t flags);
void unmap_page(page_directory_t* dir, uintptr_t virtual_addr);
uintptr_t get_physical_address(page_directory_t* dir, uintptr_t virtual_addr);
int is_page_present(page_directory_t* dir, uintptr_t virtual_addr);

void flush_tlb_single(uintptr_t virtual_addr);
void flush_tlb_full(void);
void remap_vga_buffer(void);
void page_fault_handler(registers_t* regs);
void enable_paging(uint64_t page_directory_physical);
void identity_map_range(page_directory_t* dir, uintptr_t start, uintptr_t end, uint32_t flags);

#define VIRT_TO_PHYS(virt) ((uintptr_t)(virt))
#define PHYS_TO_VIRT(phys) ((uintptr_t)(phys))
#define IS_KERNEL_ADDR(addr) ((addr) >= KERNEL_VIRTUAL_BASE)
#define IS_USER_ADDR(addr)   ((addr) < KERNEL_VIRTUAL_BASE)

#endif // ARCH_X86_64_PAGING_H
