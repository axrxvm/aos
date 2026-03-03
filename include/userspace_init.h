/*
 * === AOS HEADER BEGIN ===
 * include/userspace_init.h
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

#ifndef USERSPACE_INIT_H
#define USERSPACE_INIT_H

/**
 * Initialize userspace subsystems
 * This includes command registry, shell, and other user-facing components
 */
void userspace_init(void);

/**
 * Launch userspace shell/terminal
 * This is the main entry point for user interaction
 * Enters ring 3 (user mode) and runs userspace code
 */
void userspace_run(void);

/**
 * Legacy shell execution (runs in ring 0)
 * For backward compatibility
 */
void userspace_run_legacy(void);

#endif // USERSPACE_INIT_H
