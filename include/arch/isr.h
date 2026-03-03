/*
 * === AOS HEADER BEGIN ===
 * include/arch/isr.h
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

#ifndef ARCH_ISR_H
#define ARCH_ISR_H

#if defined(ARCH_I386)
#include <arch/i386/isr.h>
#elif defined(ARCH_X86_64)
#include <arch/x86_64/isr.h>
#else
#error "Unsupported architecture for ISR interface"
#endif

#endif // ARCH_ISR_H
