/*
 * === AOS HEADER BEGIN ===
 * include/arch/x86_64/isr.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_X86_64_ISR_H
#define ARCH_X86_64_ISR_H

#include <stdint.h>
#include <arch/x86_64/types.h>

typedef struct arch_registers registers_t;

typedef void (*isr_t)(registers_t* regs);

void register_interrupt_handler(uint8_t n, isr_t handler);
void isr_handler_common(registers_t* regs);
void irq_handler_common(registers_t* regs);

#endif // ARCH_X86_64_ISR_H
