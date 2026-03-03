/*
 * === AOS HEADER BEGIN ===
 * include/arch/x86_64/types.h
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

#ifndef ARCH_X86_64_TYPES_H
#define ARCH_X86_64_TYPES_H

#include <stdint.h>

/*
 * Keep legacy 32-bit field names to minimize churn in higher-level code.
 * Values are 64-bit in long mode.
 */
struct arch_registers {
    uint64_t ds;
    uint64_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint64_t int_no;
    uint64_t err_code;
    uint64_t ss;
    uint64_t useresp;
    uint64_t eip;
    uint64_t cs;
    uint64_t eflags;
};

struct x86_64_context {
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;
};

#endif // ARCH_X86_64_TYPES_H
