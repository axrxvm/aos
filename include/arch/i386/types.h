/*
 * === AOS HEADER BEGIN ===
 * include/arch/i386/types.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_I386_TYPES_H
#define ARCH_I386_TYPES_H

#include <stdint.h>

// i386-specific register state (pushed by interrupt handlers)
struct arch_registers {
    uint32_t ds;        // Data segment selector
    // Registers pushed by 'pusha'
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no;    // Interrupt number
    uint32_t err_code;  // Error code (if applicable)
    // Pushed by processor automatically
    uint32_t eip, cs, eflags, useresp, ss;
};

// i386-specific context for task switching
struct i386_context {
    uint32_t esp;       // Stack pointer
    uint32_t ebp;       // Base pointer
    uint32_t ebx;       // Callee-saved registers
    uint32_t esi;
    uint32_t edi;
    uint32_t eip;       // Instruction pointer (return address)
    uint32_t eflags;    // CPU flags
    uint32_t cr3;       // Page directory base (for address space switching)
};

#endif // ARCH_I386_TYPES_H
