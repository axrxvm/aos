/*
 * === AOS HEADER BEGIN ===
 * include/debug.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>
// Forward declare registers_t to avoid direct include of isr.h if not always needed
// However, panic_screen takes it, so it's better to include it.
#include <arch/i386/isr.h> // For registers_t

// Dumps the current stack trace (call stack).
void print_backtrace(uint32_t max_frames);

// Displays a panic screen with register dump and backtrace, then halts.
void panic_screen(registers_t *regs, const char *message, const char *file, uint32_t line);

// Underlying function for panic macro, allowing file/line to be passed.
void panic_msg_loc(const char *message, const char *file, uint32_t line);

// Assertion macro.
#define assert(expr) \
    ((expr) ? (void)0 : panic_msg_loc("Assertion failed: " #expr, __FILE__, __LINE__))

// Panic macro.
#define panic(msg) panic_msg_loc(msg, __FILE__, __LINE__)

#endif // DEBUG_H
