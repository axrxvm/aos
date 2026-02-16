/*
 * === AOS HEADER BEGIN ===
 * include/arch/x86_64/pit.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_X86_64_PIT_H
#define ARCH_X86_64_PIT_H

#include <stdint.h>
#include <arch/x86_64/isr.h>

#define PIT_CHANNEL0_DATA   0x40
#define PIT_COMMAND_REG     0x43

#define PIT_BASE_FREQUENCY  1193182
#define PIT_DEFAULT_DIVISOR (PIT_BASE_FREQUENCY / 100)

extern volatile uint32_t system_ticks;

void pit_init(uint16_t divisor);
void pit_set_divisor(uint16_t divisor);
void pit_handler(registers_t* regs);
uint32_t get_tick_count(void);

#endif // ARCH_X86_64_PIT_H
