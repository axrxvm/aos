/*
 * === AOS HEADER BEGIN ===
 * src/arch/x86_64/isr.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <arch/x86_64/isr.h>
#include <arch/x86_64/pic.h>
#include <debug.h>
#include <serial.h>
#include <stdlib.h>
#include <io.h>

static isr_t interrupt_handlers[256];

static const char* exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check",
    "SIMD Floating-Point Exception", "Virtualization Exception", "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception", "Reserved"
};

void isr_handler_common(registers_t* regs) {
    if (regs->int_no < 256 && interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
        return;
    }

    const char* message;
    if (regs->int_no < (sizeof(exception_messages) / sizeof(exception_messages[0]))) {
        message = exception_messages[regs->int_no];
    } else {
        message = "Reserved/Unknown CPU Exception";
    }

    panic_screen(regs, message, "CPU Exception Handler", (uint32_t)regs->int_no);

    serial_puts("Error: panic_screen returned! System will now halt.\n");
    asm volatile("cli; hlt");
}

void irq_handler_common(registers_t* regs) {
    if (regs->int_no >= 40 && regs->int_no <= 47) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);

    if (regs->int_no < 256 && interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }
}

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
    serial_puts("Registered INT ");
    char n_str[5];
    itoa(n, n_str, 10);
    serial_puts(n_str);
    serial_puts(" handler.\n");
}
