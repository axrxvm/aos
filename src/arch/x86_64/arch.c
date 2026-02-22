/*
 * === AOS HEADER BEGIN ===
 * src/arch/x86_64/arch.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <arch.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/pic.h>
#include <arch/x86_64/pit.h>
#include <arch/x86_64/isr.h>
#include <io.h>
#include <stdint.h>

const char* arch_get_name(void) {
    return "x86_64";
}

const char* arch_get_description(void) {
    return "AMD64 / Intel 64 (64-bit x86)";
}

void arch_cpu_init(void) {
    /* Long mode is entered in boot.s before kernel_main. */
}

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

void arch_register_interrupt_handler(uint8_t n, void (*handler)(void* regs)) {
    register_interrupt_handler(n, (isr_t)handler);
}

void arch_enable_irq(uint8_t irq) {
    pic_unmask_irq(irq);
}

void arch_disable_irq(uint8_t irq) {
    pic_mask_irq(irq);
}

uint32_t arch_get_kernel_code_segment(void) {
    return 0x08;
}

uint32_t arch_get_kernel_data_segment(void) {
    return 0x10;
}

uint32_t arch_get_user_code_segment(void) {
    return 0x1B;
}

uint32_t arch_get_user_data_segment(void) {
    return 0x23;
}

extern uint64_t tss_rsp0;

void arch_set_kernel_stack(uintptr_t stack) {
    tss_rsp0 = (uint64_t)stack;
}

extern volatile uint32_t system_ticks;
static uint32_t timer_frequency = 0;

extern void pit_handler(registers_t* regs);
static void arch_pit_handler_wrapper(void* regs) {
    pit_handler((registers_t*)regs);
}

void arch_timer_init(uint32_t frequency_hz) {
    uint32_t divisor = 1193182 / frequency_hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor == 0) divisor = 1;

    timer_frequency = 1193182 / divisor;

    pit_init((uint16_t)divisor);
    arch_register_interrupt_handler(32, arch_pit_handler_wrapper);
    arch_enable_irq(0);
}

uint32_t arch_timer_get_ticks(void) {
    return system_ticks;
}

uint32_t arch_timer_get_frequency(void) {
    return timer_frequency;
}

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
