/*
 * === AOS HEADER BEGIN ===
 * src/arch/i386/usermode.c
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

/*
 * enter_usermode() is now implemented in pure NASM assembly
 * at src/arch/i386/usermode_asm.s to avoid GCC inline asm issues.
 * 
 * This file is kept for any future C helper functions related to
 * user mode transitions.
 */
