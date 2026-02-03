/*
 * === AOS HEADER BEGIN ===
 * ./include/arch/i386/gdt.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef ARCH_I386_GDT_H
#define ARCH_I386_GDT_H

#include <stdint.h>

// Segment selectors
#define KERNEL_CODE_SEGMENT 0x08  // Ring 0 code (index 1)
#define KERNEL_DATA_SEGMENT 0x10  // Ring 0 data (index 2)
#define USER_CODE_SEGMENT   0x18  // Ring 3 code (index 3)
#define USER_DATA_SEGMENT   0x20  // Ring 3 data (index 4)
#define TSS_SEGMENT         0x28  // TSS (index 5)

// GDT Entry Structure
struct gdt_entry_bits {
    uint16_t limit_low;    // Lower 16 bits of limit
    uint16_t base_low;     // Lower 16 bits of base
    uint8_t  base_middle;  // Next 8 bits of base
    uint8_t  access;       // Access flags
    uint8_t  granularity;  // Granularity, limit_high (4 bits)
    uint8_t  base_high;    // Last 8 bits of base
} __attribute__((packed));
typedef struct gdt_entry_bits gdt_entry_t;

// GDT Pointer Structure (for lgdt instruction)
struct gdt_ptr_struct {
    uint16_t limit;       // Size of GDT - 1
    uint32_t base;        // Address of GDT
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;

// TSS Entry Structure (x86 Task State Segment)
struct tss_entry_struct {
    uint32_t prev_tss;   // Previous TSS (unused)
    uint32_t esp0;       // Kernel stack pointer
    uint32_t ss0;        // Kernel stack segment
    uint32_t esp1;       // Unused
    uint32_t ss1;        // Unused
    uint32_t esp2;       // Unused
    uint32_t ss2;        // Unused
    uint32_t cr3;        // Page directory base (unused, we switch manually)
    uint32_t eip;        // Instruction pointer
    uint32_t eflags;     // EFLAGS register
    uint32_t eax;        // General purpose registers
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;         // Segment selectors
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;        // LDT selector (unused)
    uint16_t trap;       // Trap on task switch
    uint16_t iomap_base; // I/O map base address
} __attribute__((packed));
typedef struct tss_entry_struct tss_entry_t;

// Public function declarations
void init_gdt();
void set_kernel_stack(uint32_t stack);
extern void gdt_load(uint32_t gdt_ptr_addr); // Assembly function
extern void tss_load(uint16_t tss_segment);   // Assembly function

#endif // ARCH_I386_GDT_H
