/*
 * === AOS HEADER BEGIN ===
 * ./include/arch/i386/isr.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef ARCH_I386_ISR_H
#define ARCH_I386_ISR_H

#include <stdint.h>

// This structure defines the registers pushed onto the stack by the ISR stubs
// and the processor. The order matters for the C handlers to correctly access them.
typedef struct {
    uint32_t ds;        // Pushed by common stub (mov ax, ds; push eax)
    // Registers pushed by 'pusha'. Order is EDI, ESI, ..., EAX on stack (low to high addr)
    // So struct members are edi, esi, ... eax to match.
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no;    // Pushed by ISR/IRQ stub before jumping to common_stub
    uint32_t err_code;  // Pushed by ISR/IRQ stub or CPU before jumping to common_stub
    // Pushed by the processor automatically on interrupt/exception
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

// Typedef for an interrupt handler function. It takes a pointer to the registers_t structure.
typedef void (*isr_t)(registers_t* regs);

// Registers a C function to handle a specific interrupt number (0-255).
void register_interrupt_handler(uint8_t n, isr_t handler);

// Common C-level handler for CPU exceptions (ISRs 0-31). Called by assembly stubs.
// Assembly pushes ESP, so C receives registers_t*
void isr_handler_common(registers_t* regs);

// Common C-level handler for hardware IRQs (ISRs 32-47). Called by assembly stubs.
// Assembly pushes ESP, so C receives registers_t*
void irq_handler_common(registers_t* regs);

#endif // ARCH_I386_ISR_H
