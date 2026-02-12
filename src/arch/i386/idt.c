/*
 * === AOS HEADER BEGIN ===
 * src/arch/i386/idt.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <arch/i386/idt.h>
#include <serial.h>
// Removed <string.h> as memset is not used; manual loop is used instead.

#define IDT_ENTRIES 256
#define KERNEL_CS 0x08  // Kernel Code Segment selector from GDT.

idt_entry_t idt_entries[IDT_ENTRIES]; // The actual IDT.
idt_ptr_t   idt_ptr;                 // Pointer structure for lidt.

// Sets an entry in the IDT.
static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low  = (base & 0xFFFF);
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].selector  = sel;
    idt_entries[num].always0   = 0;
    idt_entries[num].flags     = flags; // e.g., 0x8E for 32-bit int gate, present, DPL 0.
}

// Initializes the IDT.
void init_idt() {
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    // Zero out the IDT entries. Using manual loop for safety, as kernel's memset might be minimal.
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_entries[i].base_low = 0;
        idt_entries[i].base_high = 0;
        idt_entries[i].selector = 0;
        idt_entries[i].always0 = 0;
        idt_entries[i].flags = 0;
    }

    // Set up ISRs for CPU exceptions (0-31).
    idt_set_gate(0, (uint32_t)isr0, KERNEL_CS, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, KERNEL_CS, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, KERNEL_CS, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, KERNEL_CS, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, KERNEL_CS, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, KERNEL_CS, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, KERNEL_CS, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, KERNEL_CS, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, KERNEL_CS, 0x8E); // Double Fault
    idt_set_gate(9, (uint32_t)isr9, KERNEL_CS, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, KERNEL_CS, 0x8E); // Invalid TSS
    idt_set_gate(11, (uint32_t)isr11, KERNEL_CS, 0x8E); // Segment Not Present
    idt_set_gate(12, (uint32_t)isr12, KERNEL_CS, 0x8E); // Stack-Segment Fault
    idt_set_gate(13, (uint32_t)isr13, KERNEL_CS, 0x8E); // General Protection Fault
    idt_set_gate(14, (uint32_t)isr14, KERNEL_CS, 0x8E); // Page Fault
    idt_set_gate(15, (uint32_t)isr15, KERNEL_CS, 0x8E); // Reserved
    idt_set_gate(16, (uint32_t)isr16, KERNEL_CS, 0x8E); // x87 Floating-Point Exception
    idt_set_gate(17, (uint32_t)isr17, KERNEL_CS, 0x8E); // Alignment Check
    idt_set_gate(18, (uint32_t)isr18, KERNEL_CS, 0x8E); // Machine Check
    idt_set_gate(19, (uint32_t)isr19, KERNEL_CS, 0x8E); // SIMD Floating-Point Exception
    // ISRs 20-31 are reserved or specific.
    // The loop from the problem description was removed as it would cause issues if isr20-31 are not all defined.
    // Explicitly setting them as per existing declarations.
    idt_set_gate(20, (uint32_t)isr20, KERNEL_CS, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, KERNEL_CS, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, KERNEL_CS, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, KERNEL_CS, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, KERNEL_CS, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, KERNEL_CS, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, KERNEL_CS, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, KERNEL_CS, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, KERNEL_CS, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, KERNEL_CS, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, KERNEL_CS, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, KERNEL_CS, 0x8E);


    // Set up ISRs for hardware IRQs (32-47).
    idt_set_gate(32, (uint32_t)isr32, KERNEL_CS, 0x8E); // IRQ0: PIT
    idt_set_gate(33, (uint32_t)isr33, KERNEL_CS, 0x8E); // IRQ1: Keyboard
    idt_set_gate(34, (uint32_t)isr34, KERNEL_CS, 0x8E); // IRQ2: Cascade from slave PIC
    idt_set_gate(35, (uint32_t)isr35, KERNEL_CS, 0x8E); // IRQ3: COM2
    idt_set_gate(36, (uint32_t)isr36, KERNEL_CS, 0x8E); // IRQ4: COM1
    idt_set_gate(37, (uint32_t)isr37, KERNEL_CS, 0x8E); // IRQ5: LPT2
    idt_set_gate(38, (uint32_t)isr38, KERNEL_CS, 0x8E); // IRQ6: Floppy disk
    idt_set_gate(39, (uint32_t)isr39, KERNEL_CS, 0x8E); // IRQ7: LPT1 / Spurious
    idt_set_gate(40, (uint32_t)isr40, KERNEL_CS, 0x8E); // IRQ8: CMOS RTC
    idt_set_gate(41, (uint32_t)isr41, KERNEL_CS, 0x8E); // IRQ9: Free
    idt_set_gate(42, (uint32_t)isr42, KERNEL_CS, 0x8E); // IRQ10: Free
    idt_set_gate(43, (uint32_t)isr43, KERNEL_CS, 0x8E); // IRQ11: Free
    idt_set_gate(44, (uint32_t)isr44, KERNEL_CS, 0x8E); // IRQ12: PS/2 Mouse
    idt_set_gate(45, (uint32_t)isr45, KERNEL_CS, 0x8E); // IRQ13: FPU/Coprocessor
    idt_set_gate(46, (uint32_t)isr46, KERNEL_CS, 0x8E); // IRQ14: Primary ATA HDD
    idt_set_gate(47, (uint32_t)isr47, KERNEL_CS, 0x8E); // IRQ15: Secondary ATA HDD

    // System call gate - INT 0x80 (128)
    // DPL=3 (0xEE) to allow ring 3 user code to invoke syscalls
    extern void isr128(void);
    idt_set_gate(128, (uint32_t)isr128, KERNEL_CS, 0xEE); // INT 0x80: Syscall (DPL=3)

    // Load the IDT register.
    idt_load((uint32_t)&idt_ptr);
    serial_puts("IDT Initialized and Loaded (Exceptions & IRQs).\n");
}
