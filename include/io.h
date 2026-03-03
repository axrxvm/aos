/*
 * === AOS HEADER BEGIN ===
 * include/io.h
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


// src/include/io.h
#ifndef AOS_IO_H
#define AOS_IO_H

#include <stdint.h>

void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);
void io_wait(void);
void outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);

#endif // AOS_IO_H
