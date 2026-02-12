/*
 * === AOS HEADER BEGIN ===
 * include/arch/i386/idt.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef ARCH_I386_IDT_H
#define ARCH_I386_IDT_H

#include <stdint.h>

// Defines an entry in the Interrupt Descriptor Table.
struct idt_entry_struct {
    uint16_t base_low;    // The lower 16 bits of the ISR's address.
    uint16_t selector;    // Kernel segment selector.
    uint8_t  always0;     // This must always be zero.
    uint8_t  flags;       // Type and attributes, e.g., 0x8E for 32-bit interrupt gate.
    uint16_t base_high;   // The upper 16 bits of the ISR's address.
} __attribute__((packed));
typedef struct idt_entry_struct idt_entry_t;

// Defines the structure for the IDTR register (used with 'lidt').
struct idt_ptr_struct {
    uint16_t limit;       // The size of the IDT minus 1.
    uint32_t base;        // The base address of the IDT.
} __attribute__((packed));
typedef struct idt_ptr_struct idt_ptr_t;

// Initializes the IDT.
void init_idt(void);

// Declarations for ISR stubs (assembly functions).
// CPU Exceptions (0-31)
extern void isr0(void); extern void isr1(void); extern void isr2(void); extern void isr3(void);
extern void isr4(void); extern void isr5(void); extern void isr6(void); extern void isr7(void);
extern void isr8(void); extern void isr9(void); extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

// Hardware IRQs (32-47)
extern void isr32(void); extern void isr33(void); extern void isr34(void); extern void isr35(void);
extern void isr36(void); extern void isr37(void); extern void isr38(void); extern void isr39(void);
extern void isr40(void); extern void isr41(void); extern void isr42(void); extern void isr43(void);
extern void isr44(void); extern void isr45(void); extern void isr46(void); extern void isr47(void);

// Assembly function to load the IDT register (lidt).
// This is typically in a file like gdt_asm.s or interrupts_asm.s.
extern void idt_load(uint32_t idt_ptr_addr);

#endif // ARCH_I386_IDT_H
