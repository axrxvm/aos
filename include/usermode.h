/*
 * === AOS HEADER BEGIN ===
 * include/usermode.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

/**
 * Enter user mode (ring 3) at the specified entry point
 * This function does not return - it switches to user mode
 * 
 * @param entry_point User code entry point address
 * @param user_stack User stack pointer
 * @param argc Argument count for main()
 * @param argv Argument vector for main()
 */
void enter_usermode(uintptr_t entry_point, uintptr_t user_stack, int argc, char** argv) __attribute__((noreturn));

/**
 * Switch from ring 3 to ring 0 (handled by trap/syscall mechanism)
 * This is automatic when a syscall or interrupt occurs
 */

#endif // USERMODE_H
