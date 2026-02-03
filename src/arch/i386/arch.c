/*
 * === AOS HEADER BEGIN ===
 * ./src/arch/i386/arch.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */

#include <arch.h>
#include <arch/i386/gdt.h>
#include <arch/i386/idt.h>
#include <arch/i386/pic.h>
#include <arch/i386/pit.h>
#include <arch/i386/isr.h>
#include <io.h>
#include <stdint.h>

// Architecture name
const char* arch_get_name(void) {
    return "i386";
}

const char* arch_get_description(void) {
    return "Intel 80386 (32-bit x86)";
}

// CPU initialization
void arch_cpu_init(void) {
    init_gdt();
}

// Interrupt management
void arch_interrupts_init(void) {
    init_idt();
    pic_init();
}

void arch_enable_interrupts(void) {
    asm volatile("sti");
}

void arch_disable_interrupts(void) {
    asm volatile("cli");
}

void arch_halt(void) {
    asm volatile("hlt");
}

void arch_idle(void) {
    asm volatile("sti; hlt");
}

// Register interrupt handler (wrapper around existing i386 implementation)
void arch_register_interrupt_handler(uint8_t n, void (*handler)(void* regs)) {
    // Cast the handler to the i386-specific isr_t type
    register_interrupt_handler(n, (isr_t)handler);
}

void arch_enable_irq(uint8_t irq) {
    pic_unmask_irq(irq);
}

void arch_disable_irq(uint8_t irq) {
    pic_mask_irq(irq);
}

// Segment selector accessors (i386-specific)
uint32_t arch_get_kernel_code_segment(void) {
    return KERNEL_CODE_SEGMENT;
}

uint32_t arch_get_kernel_data_segment(void) {
    return KERNEL_DATA_SEGMENT;
}

uint32_t arch_get_user_code_segment(void) {
    return USER_CODE_SEGMENT;
}

uint32_t arch_get_user_data_segment(void) {
    return USER_DATA_SEGMENT;
}

void arch_set_kernel_stack(uint32_t stack) {
    set_kernel_stack(stack);
}

// Timer initialization
// Use the existing PIT timer system (which maintains system_ticks)
extern volatile uint32_t system_ticks;
static uint32_t timer_frequency = 0;

// Wrapper for pit_handler to match arch interrupt handler signature
extern void pit_handler(registers_t *regs);
static void arch_pit_handler_wrapper(void* regs) {
    pit_handler((registers_t*)regs);
}

void arch_timer_init(uint32_t frequency_hz) {
    // Calculate PIT divisor for desired frequency
    // PIT base frequency is 1193182 Hz
    uint32_t divisor = 1193182 / frequency_hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor == 0) divisor = 1;
    
    // Store actual frequency
    timer_frequency = 1193182 / divisor;
    
    pit_init((uint16_t)divisor);
    
    // Register the PIT handler wrapper
    arch_register_interrupt_handler(32, arch_pit_handler_wrapper);
    arch_enable_irq(0);  // Enable timer IRQ
}

uint32_t arch_timer_get_ticks(void) {
    return system_ticks;
}

uint32_t arch_timer_get_frequency(void) {
    return timer_frequency;
}

// I/O port access (i386 supports this natively)
#define ARCH_HAS_IO_PORTS 1

uint8_t arch_io_inb(uint16_t port) {
    return inb(port);
}

uint16_t arch_io_inw(uint16_t port) {
    return inw(port);
}

uint32_t arch_io_inl(uint16_t port) {
    return inl(port);
}

void arch_io_outb(uint16_t port, uint8_t value) {
    outb(port, value);
}

void arch_io_outw(uint16_t port, uint16_t value) {
    outw(port, value);
}

void arch_io_outl(uint16_t port, uint32_t value) {
    outl(port, value);
}
