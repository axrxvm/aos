/*
 * === AOS HEADER BEGIN ===
 * include/panic.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/*
 * DEVELOPER_NOTE_BLOCK
 * Module Overview:
 * - This file is part of the aOS production kernel/userspace codebase.
 * - Review public symbols in this unit to understand contracts with adjacent modules.
 * - Keep behavior-focused comments near non-obvious invariants, state transitions, and safety checks.
 * - Avoid changing ABI/data-layout assumptions without updating dependent modules.
 */


#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
// Include debug.h for the new panic() macro and related functions.
#include <debug.h>

// The old panic function declaration is no longer needed if all call sites
// are updated to use the panic() macro from debug.h.
// void panic(const char *message); // Legacy

#endif // PANIC_H
