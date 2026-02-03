/*
 * === AOS HEADER BEGIN ===
 * ./include/arch/i386/pit.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef PIT_H
#define PIT_H

#include <stdint.h>
#include <arch/i386/isr.h> // For registers_t type, needed for pit_handler signature.

#define PIT_CHANNEL0_DATA   0x40    // PIT Channel 0 data port.
#define PIT_COMMAND_REG     0x43    // PIT Command Register.

#define PIT_BASE_FREQUENCY  1193182 // PIT's input clock frequency in Hz.
// Default divisor for ~100 Hz: 1193182 / 100 = 11931.82. Rounded to 11932.
#define PIT_DEFAULT_DIVISOR (PIT_BASE_FREQUENCY / 100)

extern volatile uint32_t system_ticks; // Incremented by PIT IRQ, tracks uptime.

// Initializes the PIT with a given divisor.
void pit_init(uint16_t divisor);

// Sets the PIT's divisor for Channel 0.
void pit_set_divisor(uint16_t divisor);

// The C part of the PIT's interrupt service routine (ISR).
// Called from the assembly stub for IRQ0 (INT 32).
void pit_handler(registers_t *regs);

// Get the current system tick count
uint32_t get_tick_count(void);

#endif // PIT_H
