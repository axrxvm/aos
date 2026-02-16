/*
 * === AOS HEADER BEGIN ===
 * src/arch/x86_64/pit.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <arch/x86_64/pit.h>
#include <io.h>
#include <serial.h>
#include <stdlib.h>

extern void net_poll(void);

volatile uint32_t system_ticks = 0;

void pit_init(uint16_t divisor) {
    pit_set_divisor(divisor);

    serial_puts("PIT Initialized. Target frequency: ~");
    char freq_str[12];

    uint16_t display_divisor = (divisor == 0) ? 65536 : divisor;
    uint32_t freq = PIT_BASE_FREQUENCY / display_divisor;

    itoa(freq, freq_str, 10);
    serial_puts(freq_str);
    serial_puts(" Hz (Divisor: ");
    itoa(divisor, freq_str, 10);
    serial_puts(freq_str);
    serial_puts(")\n");
}

void pit_set_divisor(uint16_t divisor) {
    outb(PIT_COMMAND_REG, 0x34);
    io_wait();
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    io_wait();
}

void pit_handler(registers_t* regs) {
    (void)regs;
    system_ticks++;

    if ((system_ticks % 10) == 0) {
        net_poll();
    }
}

uint32_t get_tick_count(void) {
    return system_ticks;
}
