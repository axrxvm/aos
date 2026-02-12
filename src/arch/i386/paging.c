/*
 * === AOS HEADER BEGIN ===
 * src/arch/i386/paging.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <arch/i386/paging.h>
#include <arch/i386/isr.h>
#include <memory.h>
#include <pmm.h>
#include <serial.h>
#include <stdlib.h>
#include <panic.h>
#include <debug.h>
#include <string.h>

// Current page directory
page_directory_t *current_directory = 0;
page_directory_t *kernel_directory = 0;

// Assembly functions for low-level paging operations
extern void enable_paging_asm(uint32_t page_directory_physical);
extern void load_page_directory(uint32_t page_directory_physical);
extern uint32_t get_cr3(void);

void init_paging(void) {
    serial_puts("Initializing paging system...\n");
    
    // Create the kernel page directory
    serial_puts("Creating kernel page directory...\n");
    kernel_directory = create_page_directory();
    if (!kernel_directory) {
        serial_puts("FATAL: Failed to create kernel page directory\n");
        asm volatile("cli; hlt");
        while(1);
    }
    serial_puts("Kernel page directory created.\n");
    
    // Identity map the first 8MB (0x0 - 0x800000) for kernel, data, and heap
    serial_puts("Identity mapping first 8MB...\n");
    for (uint32_t addr = 0; addr < 0x800000; addr += PAGE_SIZE) {
        map_page(kernel_directory, addr, addr, PAGE_PRESENT | PAGE_WRITE);
    }
    serial_puts("Identity mapping complete.\n");
    
    // Map VGA buffer
    map_page(kernel_directory, 0xB8000, 0xB8000, PAGE_PRESENT | PAGE_WRITE);
    
    // Don't map to higher half yet - that requires relocating the kernel
    // For now, stick with identity mapping
    
    // Register page fault handler
    register_interrupt_handler(14, page_fault_handler);
    
    // Enable paging
    switch_page_directory(kernel_directory);
    enable_paging_asm(kernel_directory->physical_addr);
    
    serial_puts("Paging enabled successfully!\n");
}

page_directory_t *create_page_directory(void) {
    // Allocate structure (now only 12 bytes with pointer)
    page_directory_t *dir = (page_directory_t *)alloc_page();
    if (!dir) {
        serial_puts("CRITICAL: create_page_directory - failed to alloc directory structure\n");
        return 0;
    }
    
    // CRITICAL: Initialize ALL fields to zero/NULL immediately
    dir->cpu_dir = 0;
    dir->tables_physical = 0;
    dir->physical_addr = 0;
    
    // Allocate tables_physical array (1024 pointers * 4 bytes = 4KB)
    dir->tables_physical = (page_table_t **)alloc_page();
    if (!dir->tables_physical) {
        serial_puts("CRITICAL: create_page_directory - failed to alloc tables_physical\n");
        free_page(dir);
        return 0;
    }
    
    // CRITICAL: Zero all table pointers (byte-by-byte to avoid memset issues)
    uint8_t *clear_ptr = (uint8_t *)dir->tables_physical;
    for (uint32_t i = 0; i < PAGE_SIZE; i++) {
        clear_ptr[i] = 0;
    }
    
    // Allocate CPU-visible page directory (4KB)
    dir->cpu_dir = (page_directory_cpu_t *)alloc_page();
    if (!dir->cpu_dir) {
        serial_puts("CRITICAL: create_page_directory - failed to alloc cpu_dir\n");
        free_page(dir->tables_physical);
        free_page(dir);
        return 0;
    }
    
    // CRITICAL: Zero all CPU directory entries (byte-by-byte)
    clear_ptr = (uint8_t *)dir->cpu_dir;
    for (uint32_t i = 0; i < PAGE_SIZE; i++) {
        clear_ptr[i] = 0;
    }
    
    // Store physical address for CR3 (identity mapped, so virtual == physical)
    dir->physical_addr = (uint32_t)dir->cpu_dir;
    
    // Validate the address is reasonable
    if (dir->physical_addr == 0 || dir->physical_addr > 0x10000000) {
        serial_puts("CRITICAL: create_page_directory - invalid physical_addr\n");
        free_page(dir->cpu_dir);
        free_page(dir->tables_physical);
        free_page(dir);
        return 0;
    }
    
    serial_puts("Page directory created at phys: 0x");
    char addr_str[12];
    itoa(dir->physical_addr, addr_str, 16);
    serial_puts(addr_str);
    serial_puts("\n");
    
    return dir;
}

void destroy_page_directory(page_directory_t *dir) {
    if (!dir || dir == kernel_directory) {
        return; // Don't destroy kernel directory
    }
    
    // Free all page tables
    if (dir->tables_physical) {
        for (int i = 0; i < TABLES_PER_DIR; i++) {
            if (dir->tables_physical[i]) {
                free_page((void *)dir->tables_physical[i]);
            }
        }
        // Free the tables_physical array itself
        free_page((void *)dir->tables_physical);
    }
    
    // Free the CPU page directory
    if (dir->cpu_dir) {
        free_page((void *)dir->cpu_dir);
    }
    
    // Free the directory structure itself
    free_page((void *)dir);
}

void map_page(page_directory_t *dir, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    // Critical: Validate ALL inputs before proceeding
    if (!dir) {
        serial_puts("CRITICAL: map_page called with NULL directory\n");
        asm volatile("cli; hlt");
        while(1);
    }
    
    if (!dir->cpu_dir) {
        serial_puts("CRITICAL: map_page - cpu_dir is NULL\n");
        asm volatile("cli; hlt");
        while(1);
    }
    
    if (!dir->tables_physical) {
        serial_puts("CRITICAL: map_page - tables_physical is NULL\n");
        asm volatile("cli; hlt");
        while(1);
    }
    
    // Align addresses to page boundaries (must be 4KB aligned)
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    physical_addr = PAGE_ALIGN_DOWN(physical_addr);
    
    // Validate addresses are within reasonable range
    if (physical_addr == 0 && virtual_addr != 0) {
        serial_puts("WARNING: Mapping to physical address 0 (except for null page)\n");
    }
    
    // Get page directory and page table indices
    uint32_t dir_index = virtual_addr >> 22;  // Top 10 bits
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;  // Middle 10 bits
    
    // Validate indices are within bounds
    if (dir_index >= TABLES_PER_DIR || table_index >= PAGES_PER_TABLE) {
        serial_puts("CRITICAL: map_page - index out of bounds\n");
        asm volatile("cli; hlt");
        while(1);
    }
    
    // Check if page table exists
    if (!dir->tables_physical[dir_index]) {
        // Allocate new page table
        page_table_t *table = (page_table_t *)alloc_page();
        if (!table) {
            serial_puts("CRITICAL: map_page - Failed to allocate page table (out of memory)\n");
            asm volatile("cli; hlt");
            while(1);
        }
        
        // CRITICAL: Zero the entire table before use
        // Use byte-by-byte clearing to avoid any memset issues
        uint8_t *p = (uint8_t *)table;
        for (uint32_t i = 0; i < sizeof(page_table_t); i++) {
            p[i] = 0;
        }
        
        // Store physical address for bookkeeping
        dir->tables_physical[dir_index] = table;
        
        // Set page directory entry with physical address (identity mapped)
        uint32_t table_phys = (uint32_t)table;
        
        // Validate the table address is reasonable
        if (table_phys == 0) {
            serial_puts("CRITICAL: map_page - Allocated table has address 0\n");
            asm volatile("cli; hlt");
            while(1);
        }
        
        // Page directory entry needs USER flag if any page in the table needs it
        dir->cpu_dir->entries[dir_index] = table_phys | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    } else {
        // Update existing page directory entry to include USER flag if needed
        if (flags & PAGE_USER) {
            dir->cpu_dir->entries[dir_index] |= PAGE_USER;
        }
    }
    
    // Get the page table - double-check it's valid
    page_table_t *table = dir->tables_physical[dir_index];
    if (!table) {
        serial_puts("CRITICAL: map_page - Page table is NULL after allocation\n");
        asm volatile("cli; hlt");
        while(1);
    }
    
    // Set page table entry with full flags
    table->pages[table_index] = physical_addr | (flags & 0xFFF);
    
    // Flush TLB for this page if we're currently using this directory
    if (dir == current_directory) {
        flush_tlb_single(virtual_addr);
    }
}

void unmap_page(page_directory_t *dir, uint32_t virtual_addr) {
    if (!dir || !dir->tables_physical) {
        serial_puts("WARNING: unmap_page - invalid directory\n");
        return;
    }
    
    virtual_addr = PAGE_ALIGN_DOWN(virtual_addr);
    
    uint32_t dir_index = virtual_addr >> 22;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;
    
    // Validate indices
    if (dir_index >= TABLES_PER_DIR || table_index >= PAGES_PER_TABLE) {
        serial_puts("WARNING: unmap_page - index out of bounds\n");
        return;
    }
    
    // Check if page table exists
    if (!dir->tables_physical[dir_index]) {
        return; // Page not mapped
    }
    
    // Get the page table and clear the entry
    page_table_t *table = dir->tables_physical[dir_index];
    table->pages[table_index] = 0;
    
    // Flush TLB
    if (dir == current_directory) {
        flush_tlb_single(virtual_addr);
    }
}

uint32_t get_physical_address(page_directory_t *dir, uint32_t virtual_addr) {
    if (!dir || !dir->tables_physical) {
        return 0;
    }
    
    uint32_t dir_index = virtual_addr >> 22;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;
    uint32_t offset = virtual_addr & 0xFFF;
    
    // Validate indices
    if (dir_index >= TABLES_PER_DIR || table_index >= PAGES_PER_TABLE) {
        return 0;
    }
    
    // Check if page table exists
    if (!dir->tables_physical[dir_index]) {
        return 0; // Page not mapped
    }
    
    // Get page table entry
    page_table_t *table = dir->tables_physical[dir_index];
    uint32_t page_entry = table->pages[table_index];
    
    if (!(page_entry & PAGE_PRESENT)) {
        return 0; // Page not present
    }
    
    return PAGE_GET_ADDR(page_entry) + offset;
}

int is_page_present(page_directory_t *dir, uint32_t virtual_addr) {
    if (!dir || !dir->tables_physical) {
        return 0;
    }
    
    uint32_t dir_index = virtual_addr >> 22;
    uint32_t table_index = (virtual_addr >> 12) & 0x3FF;
    
    // Validate indices
    if (dir_index >= TABLES_PER_DIR || table_index >= PAGES_PER_TABLE) {
        return 0;
    }
    
    if (!dir->tables_physical[dir_index]) {
        return 0;
    }
    
    page_table_t *table = dir->tables_physical[dir_index];
    return (table->pages[table_index] & PAGE_PRESENT) ? 1 : 0;
}

void switch_page_directory(page_directory_t *dir) {
    current_directory = dir;
    load_page_directory(dir->physical_addr);
}

void page_fault_handler(registers_t *regs) {
    // Get the faulting address from CR2
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));
    
    // Analyze the error code
    int present = !(regs->err_code & 0x1);    // Page not present
    int write = regs->err_code & 0x2;         // Write operation?
    int user = regs->err_code & 0x4;          // Processor was in user mode?
    int reserved = regs->err_code & 0x8;      // Overwritten CPU-reserved bits?
    int fetch = regs->err_code & 0x10;        // Instruction fetch?
    
    // Print error information to serial (for debugging)
    serial_puts("\n=== PAGE FAULT ===");
    serial_puts("\nFaulting address: 0x");
    char addr_str[12];
    itoa(faulting_address, addr_str, 16);
    serial_puts(addr_str);
    serial_puts("\nEIP: 0x");
    itoa(regs->eip, addr_str, 16);
    serial_puts(addr_str);
    serial_puts("\nError code: 0x");
    itoa(regs->err_code, addr_str, 16);
    serial_puts(addr_str);
    serial_puts("\nFlags: ");
    if (present) serial_puts("[NOT_PRESENT] ");
    if (write) serial_puts("[WRITE] ");
    if (user) serial_puts("[USER_MODE] ");
    if (reserved) serial_puts("[RESERVED] ");
    if (fetch) serial_puts("[FETCH] ");
    serial_puts("\n");
    
    // Build descriptive panic message
    char message[128];
    const char* prefix = "Page fault at 0x";
    const char* mid;
    
    if (faulting_address < 0x1000) {
        mid = " (null pointer dereference)";
    } else if (faulting_address >= 0xC0000000) {
        mid = " (kernel space access)";
    } else if (faulting_address >= 0x500000 && faulting_address < 0x700000) {
        mid = " (heap access)";
    } else {
        mid = "";
    }
    
    // Construct message
    int pos = 0;
    while (*prefix && pos < 110) message[pos++] = *prefix++;
    itoa(faulting_address, addr_str, 16);
    char* p = addr_str;
    while (*p && pos < 115) message[pos++] = *p++;
    p = (char*)mid;
    while (*p && pos < 127) message[pos++] = *p++;
    message[pos] = '\0';
    
    serial_puts("==================\n");
    
    // Enter KRM via panic_screen - this does not return
    panic_screen(regs, message, __FILE__, __LINE__);
    
    // Should never reach here, but just in case
    asm volatile("cli");
    while(1) {
        asm volatile("hlt");
    }
}

void flush_tlb_single(uint32_t virtual_addr) {
    asm volatile("invlpg (%0)" :: "r" (virtual_addr) : "memory");
}

void flush_tlb_full(void) {
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r" (cr3));
    asm volatile("mov %0, %%cr3" :: "r" (cr3) : "memory");
}

// Identity map a range of addresses (helper function for arch abstraction layer)
void identity_map_range(page_directory_t *dir, uint32_t start, uint32_t end, uint32_t flags) {
    for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
        map_page(dir, addr, addr, flags);
    }
}
