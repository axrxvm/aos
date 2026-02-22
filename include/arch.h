/*
 * === AOS HEADER BEGIN ===
 * include/arch.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef ARCH_H
#define ARCH_H

#include <stdint.h>
#include <stdbool.h>

// Architecture-independent CPU initialization and control
void arch_cpu_init(void);          // Initialize CPU-specific features (GDT, etc.)
void arch_enable_interrupts(void);  // Enable hardware interrupts
void arch_disable_interrupts(void); // Disable hardware interrupts
void arch_halt(void);              // Halt the CPU
void arch_idle(void);              // Idle the CPU (halt with interrupts enabled)

// Architecture-independent interrupt management
void arch_interrupts_init(void);   // Initialize interrupt system (IDT, PIC, etc.)
void arch_register_interrupt_handler(uint8_t n, void (*handler)(void* regs));
void arch_enable_irq(uint8_t irq);  // Enable specific IRQ line
void arch_disable_irq(uint8_t irq); // Disable specific IRQ line

// Architecture-independent timer
void arch_timer_init(uint32_t frequency_hz); // Initialize system timer
uint32_t arch_timer_get_ticks(void);         // Get current timer tick count
uint32_t arch_timer_get_frequency(void);     // Get timer frequency in Hz

// Architecture-independent context structure (opaque to generic code)
typedef struct arch_context arch_context_t;

// Architecture-independent context switching
void arch_context_init(arch_context_t* ctx, void* stack, uint32_t stack_size, void (*entry)(void));
void arch_context_switch(arch_context_t* old_ctx, arch_context_t* new_ctx);

// Architecture-independent I/O port access (for architectures that support it)
#if defined(ARCH_HAS_IO_PORTS)
uint8_t arch_io_inb(uint16_t port);
uint16_t arch_io_inw(uint16_t port);
uint32_t arch_io_inl(uint16_t port);
void arch_io_outb(uint16_t port, uint8_t value);
void arch_io_outw(uint16_t port, uint16_t value);
void arch_io_outl(uint16_t port, uint32_t value);
#endif

// Architecture name and information
const char* arch_get_name(void);
const char* arch_get_description(void);

// Architecture-specific segment selectors (for architectures that use segmentation)
#if defined(ARCH_HAS_SEGMENTATION)
uint32_t arch_get_kernel_code_segment(void);
uint32_t arch_get_kernel_data_segment(void);
uint32_t arch_get_user_code_segment(void);
uint32_t arch_get_user_data_segment(void);
void arch_set_kernel_stack(uintptr_t stack);
#endif

#endif // ARCH_H
