/*
 * === AOS HEADER BEGIN ===
 * include/arch/paging.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_PAGING_ARCH_H
#define ARCH_PAGING_ARCH_H

#if defined(ARCH_I386)
#include <arch/i386/paging.h>
#elif defined(ARCH_X86_64)
#include <arch/x86_64/paging.h>
#else
#error "Unsupported architecture for paging interface"
#endif

#endif // ARCH_PAGING_ARCH_H
