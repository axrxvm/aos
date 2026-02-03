/*
 * === AOS HEADER BEGIN ===
 * ./src/arch/i386/isr.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <arch/i386/isr.h>
#include <arch/i386/pic.h> // For pic_send_eoi() and PIC commands
#include <serial.h>        // For printing debug messages
#include <stdlib.h>        // For itoa()
// #include <panic.h>      // Old panic.h, new one includes debug.h, or directly include debug.h
#include <debug.h>         // For panic_screen and other debug facilities
#include <io.h>            // For outb()

// Array of registered C interrupt handlers. Indexed by interrupt number.
static isr_t interrupt_handlers[256]; // Initialized to 0 (NULL pointers) by C default for static storage.

// Messages for CPU exceptions.
const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint", "Into Detected Overflow",
    "Out of Bounds", "Invalid Opcode", "No Coprocessor", "Double Fault", "Coprocessor Segment Overrun",
    "Bad TSS", "Segment Not Present", "Stack Fault", "General Protection Fault", "Page Fault",
    "Unknown Interrupt (Reserved by Intel)", "Coprocessor Fault (x87 FPU)", "Alignment Check", "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception", // 20
    "Control Protection Exception", // 21 (AMD-specific or newer Intel)
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception", // 28
    "VMM Communication Exception",  // 29
    "Security Exception",           // 30
    "Reserved"                      // 31
};

// Common handler for CPU exceptions (ISRs 0-31).
// regs pointer is passed from assembly stub via 'push esp; call isr_handler_common'
void isr_handler_common(registers_t* regs) {
    // Disable further interrupts if this handler is re-entrant or something goes wrong.
    // asm volatile("cli"); // panic_screen will do this.

    const char *message;
    if (regs->int_no < sizeof(exception_messages) / sizeof(const char *)) {
        message = exception_messages[regs->int_no];
    } else {
        message = "Reserved/Unknown CPU Exception";
    }

    // Call the new panic_screen function.
    // For file and line, these are not available directly from a CPU exception.
    // We can pass "CPU Exception Handler" or similar as the file, and int_no as a placeholder for line.
    // panic_screen is expected to be linked.
    panic_screen(regs, message, "CPU Exception Handler", regs->int_no);

    // The call to panic_screen will not return as it halts the system.
    // So, code here should not be reached. If it is, something is very wrong.
    // serial_puts is expected to be linked if used in this unlikely path.
    serial_puts("Error: panic_screen returned! System will now halt.\n");
    asm volatile ("cli; hlt"); // Ensure halt if panic_screen returns.
}

// Common handler for hardware IRQs (ISRs 32-47).
// regs pointer is passed from assembly stub via 'push esp; call irq_handler_common'
void irq_handler_common(registers_t* regs) {
    // Send EOI (End of Interrupt) to the PICs.
    // If the IRQ came from the slave PIC (IRQ 8-15, which are INT 40-47),
    // an EOI must be sent to it.
    if (regs->int_no >= 40 && regs->int_no <= 47) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    // An EOI must always be sent to the master PIC for any IRQ.
    outb(PIC1_COMMAND, PIC_EOI);

    // Call the registered C handler for this IRQ, if one exists.
    if (interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs); // Pass the registers pointer to the specific handler.
    } else {
        // Optionally handle unexpected/unregistered IRQs.
        // Spurious IRQ7 or IRQ15 might occur.
        // For now, if no handler, it's silently ignored after EOI.
        /*
        if (itoa != NULL && serial_puts != NULL) { // Defensive check
            serial_puts("Unhandled IRQ: ");
            char n_str[4];
            itoa(regs->int_no, n_str, 10);
            serial_puts(n_str);
            serial_puts("\n");
        }
        */
    }
}

// Registers a C function for a given interrupt number.
void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
    // Optional: Print confirmation message.
    // itoa and serial_puts are expected to be linked.
    serial_puts("Registered INT ");
    char n_str[5];
    itoa(n, n_str, 10);
    serial_puts(n_str);
    serial_puts(" handler.\n");
}
