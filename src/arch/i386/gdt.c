/*
 * === AOS HEADER BEGIN ===
 * ./src/arch/i386/gdt.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <arch/i386/gdt.h> // Updated include path
#include <string.h>        // For memset

#define GDT_ENTRIES 6 // Null, Kernel Code, Kernel Data, User Code, User Data, TSS

gdt_entry_t gdt_entries[GDT_ENTRIES];
gdt_ptr_t   gdt_ptr;
tss_entry_t tss_entry;

// Helper function to create a GDT entry
static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

void init_gdt() {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    // Null descriptor
    gdt_set_gate(0, 0, 0, 0, 0);

    // Kernel Code Segment: base=0, limit=4GB, access=0x9A (present, ring 0, code, executable, readable)
    // Granularity=0xCF (4KB blocks, 32-bit protected mode)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // Kernel Data Segment: base=0, limit=4GB, access=0x92 (present, ring 0, data, writable)
    // Granularity=0xCF (4KB blocks, 32-bit protected mode)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // User Code Segment: base=0, limit=4GB, access=0xFA (present, ring 3, code, executable, readable)
    // Granularity=0xCF (4KB blocks, 32-bit protected mode)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // User Data Segment: base=0, limit=4GB, access=0xF2 (present, ring 3, data, writable)
    // Granularity=0xCF (4KB blocks, 32-bit protected mode)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // Initialize TSS
    memset(&tss_entry, 0, sizeof(tss_entry));
    tss_entry.ss0 = 0x10;  // Kernel data segment
    tss_entry.esp0 = 0;     // Will be set during context switch
    tss_entry.cs = 0x0b;    // Kernel code segment | Ring 3
    tss_entry.ss = 0x13;    // User data segment | Ring 3
    tss_entry.ds = 0x13;
    tss_entry.es = 0x13;
    tss_entry.fs = 0x13;
    tss_entry.gs = 0x13;

    // TSS Segment: access=0x89 (present, ring 0, TSS, accessed)
    uint32_t tss_base = (uint32_t)&tss_entry;
    uint32_t tss_limit = sizeof(tss_entry);
    gdt_set_gate(5, tss_base, tss_limit, 0x89, 0x40);

    // Load the GDT
    gdt_load((uint32_t)&gdt_ptr);

    // Load TSS (segment selector 0x28 = 5 * 8)
    tss_load(0x28);
}

void set_kernel_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}
