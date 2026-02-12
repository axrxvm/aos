/*
 * === AOS HEADER BEGIN ===
 * include/arch/i386/paging.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef ARCH_I386_PAGING_H
#define ARCH_I386_PAGING_H

#include <stdint.h>
#include <arch/i386/isr.h>

// Page size is 4KB
#define PAGE_SIZE 4096
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

// Virtual memory layout
#define KERNEL_VIRTUAL_BASE 0xC0000000  // 3GB mark - kernel space
#define USER_VIRTUAL_BASE   0x00000000  // 0GB - 3GB user space
#define KERNEL_HEAP_START   0xC1000000  // Kernel heap starts at 3GB + 16MB

// Page directory/table entries
#define PAGES_PER_TABLE     1024
#define TABLES_PER_DIR      1024
#define PAGE_DIRECTORY_SIZE (TABLES_PER_DIR * sizeof(uint32_t))
#define PAGE_TABLE_SIZE     (PAGES_PER_TABLE * sizeof(uint32_t))

// Page flags
#define PAGE_PRESENT        0x001   // Page is present in memory
#define PAGE_WRITE          0x002   // Page is writable
#define PAGE_USER           0x004   // Page is accessible from user mode
#define PAGE_WRITETHROUGH   0x008   // Write-through caching
#define PAGE_NOCACHE        0x010   // Disable caching
#define PAGE_ACCESSED       0x020   // Page has been accessed (set by CPU)
#define PAGE_DIRTY          0x040   // Page has been written to (set by CPU)
#define PAGE_SIZE_FLAG      0x080   // 4MB page (only in page directory)
#define PAGE_GLOBAL         0x100   // Global page (not flushed on CR3 load)

// Extract address from page table entry
#define PAGE_GET_ADDR(entry) ((entry) & 0xFFFFF000)
#define PAGE_GET_FLAGS(entry) ((entry) & 0x00000FFF)

// Page directory and page table structures
typedef struct {
    uint32_t pages[PAGES_PER_TABLE];
} page_table_t;

// Page directory structure
// The actual page directory (1024 entries) that the CPU sees
typedef struct {
    uint32_t entries[TABLES_PER_DIR];
} page_directory_cpu_t;

// Our kernel's representation of a page directory
typedef struct {
    page_directory_cpu_t *cpu_dir;    // Points to the actual 4KB-aligned page directory
    page_table_t **tables_physical;   // Pointer to array of physical addresses (dynamically allocated)
    uint32_t physical_addr;           // Physical address of the CPU directory
} page_directory_t;

// Current page directory
extern page_directory_t *current_directory;
extern page_directory_t *kernel_directory;

// Paging functions
void init_paging(void);
void switch_page_directory(page_directory_t *dir);
page_directory_t *create_page_directory(void);
void destroy_page_directory(page_directory_t *dir);

// Page management
void map_page(page_directory_t *dir, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
void unmap_page(page_directory_t *dir, uint32_t virtual_addr);
uint32_t get_physical_address(page_directory_t *dir, uint32_t virtual_addr);
int is_page_present(page_directory_t *dir, uint32_t virtual_addr);

// Page fault handler
void page_fault_handler(registers_t *regs);

// Utility functions
void flush_tlb_single(uint32_t virtual_addr);
void flush_tlb_full(void);
void enable_paging(uint32_t page_directory_physical);
void identity_map_range(page_directory_t *dir, uint32_t start, uint32_t end, uint32_t flags);

// Virtual to physical address conversion macros for kernel
#define VIRT_TO_PHYS(virt) ((uint32_t)(virt) - KERNEL_VIRTUAL_BASE)
#define PHYS_TO_VIRT(phys) ((uint32_t)(phys) + KERNEL_VIRTUAL_BASE)

// Check if address is in kernel space
#define IS_KERNEL_ADDR(addr) ((addr) >= KERNEL_VIRTUAL_BASE)
#define IS_USER_ADDR(addr) ((addr) < KERNEL_VIRTUAL_BASE)

#endif // ARCH_I386_PAGING_H
