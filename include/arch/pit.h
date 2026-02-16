/*
 * === AOS HEADER BEGIN ===
 * include/arch/pit.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_PIT_H
#define ARCH_PIT_H

#if defined(ARCH_I386)
#include <arch/i386/pit.h>
#elif defined(ARCH_X86_64)
#include <arch/x86_64/pit.h>
#else
#error "Unsupported architecture for PIT interface"
#endif

#endif // ARCH_PIT_H
