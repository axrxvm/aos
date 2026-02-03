/*
 * === AOS HEADER BEGIN ===
 * ./include/arch_paging.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */

#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H

#include <stdint.h>
#include <stdbool.h>

// Page size (architecture may override this, but 4KB is common)
#ifndef ARCH_PAGE_SIZE
#define ARCH_PAGE_SIZE 4096
#endif

// Page alignment macros
#define ARCH_PAGE_ALIGN_DOWN(addr) ((addr) & ~(ARCH_PAGE_SIZE - 1))
#define ARCH_PAGE_ALIGN_UP(addr) (((addr) + ARCH_PAGE_SIZE - 1) & ~(ARCH_PAGE_SIZE - 1))

// Page flags (architecture-independent abstractions)
#define ARCH_PAGE_PRESENT    0x01   // Page is present in memory
#define ARCH_PAGE_WRITABLE   0x02   // Page is writable
#define ARCH_PAGE_USER       0x04   // Page is accessible from user mode
#define ARCH_PAGE_NOCACHE    0x08   // Disable caching

// Opaque page directory type (architecture-specific implementation)
typedef struct arch_page_directory arch_page_directory_t;

// Architecture-independent paging functions
void arch_paging_init(void);                                    // Initialize paging subsystem
arch_page_directory_t* arch_paging_create_directory(void);      // Create a new page directory
void arch_paging_destroy_directory(arch_page_directory_t* dir); // Destroy a page directory
void arch_paging_switch_directory(arch_page_directory_t* dir);  // Switch to a page directory
arch_page_directory_t* arch_paging_get_current_directory(void); // Get current page directory

// Map a virtual address to a physical address
bool arch_paging_map(arch_page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags);

// Unmap a virtual address
bool arch_paging_unmap(arch_page_directory_t* dir, uint32_t virt);

// Get physical address from virtual address (returns 0 if not mapped)
uint32_t arch_paging_get_physical(arch_page_directory_t* dir, uint32_t virt);

// Identity map a range of physical addresses
bool arch_paging_identity_map_range(arch_page_directory_t* dir, uint32_t start, uint32_t end, uint32_t flags);

// Page fault handler (called by architecture-specific interrupt handler)
void arch_paging_fault_handler(void* regs);

#endif // ARCH_PAGING_H
