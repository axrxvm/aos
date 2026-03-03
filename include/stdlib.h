/*
 * === AOS HEADER BEGIN ===
 * include/stdlib.h
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


#ifndef AOS_STDLIB_H
#define AOS_STDLIB_H

#include <stdint.h> // For uint32_t

void itoa(uint32_t num, char *buf, int base);
int atoi(const char *str);

#endif // AOS_STDLIB_H
