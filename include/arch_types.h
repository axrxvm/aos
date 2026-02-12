/*
 * === AOS HEADER BEGIN ===
 * include/arch_types.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_TYPES_H
#define ARCH_TYPES_H

#include <stdint.h>

// Architecture-specific register state (opaque to generic code)
// Each architecture defines this in their own arch/<arch>/types.h
typedef struct arch_registers arch_registers_t;

// Architecture-specific context (for task switching)
typedef struct arch_context {
    void* arch_specific_data;  // Pointer to architecture-specific context data
} arch_context_t;

// Include architecture-specific types
#ifdef ARCH_I386
#include <arch/i386/types.h>
#elif defined(ARCH_X86_64)
#include <arch/x86_64/types.h>
#elif defined(ARCH_ARM)
#include <arch/arm/types.h>
#elif defined(ARCH_RISCV)
#include <arch/riscv/types.h>
#else
#error "No architecture defined! Please define ARCH_I386, ARCH_X86_64, ARCH_ARM, or ARCH_RISCV"
#endif

#endif // ARCH_TYPES_H
