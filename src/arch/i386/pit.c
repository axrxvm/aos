/*
 * === AOS HEADER BEGIN ===
 * src/arch/i386/pit.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <arch/pit.h>
#include <io.h>     // For outb() and io_wait().
#include <serial.h> // For serial_puts() for debugging.
#include <stdlib.h> // For itoa() (if available and functional).

// Forward declaration for scheduler_tick (from process manager)
extern void scheduler_tick(void);

// Forward declaration for network polling
extern void net_poll(void);

// Global tick counter, incremented by the PIT handler.
volatile uint32_t system_ticks = 0;

// Initializes PIT Channel 0 to the specified divisor.
void pit_init(uint16_t divisor) {
    pit_set_divisor(divisor);
    serial_puts("PIT Initialized. Target frequency: ~");
    char freq_str[12]; // Buffer for itoa conversion

    // Calculate approximate frequency. Avoid division by zero if divisor is 0, though
    // PIT hardware treats divisor 0 as 65536. For display, ensure divisor is non-zero.
    uint16_t display_divisor = (divisor == 0) ? 65536 : divisor;
    uint32_t freq = PIT_BASE_FREQUENCY / display_divisor;

    // itoa is expected to be linked; removed != NULL check.
    itoa(freq, freq_str, 10);
    serial_puts(freq_str);
    serial_puts(" Hz (Divisor: ");
    itoa(divisor, freq_str, 10); // Show the actual divisor passed (0 is valid for PIT)
    serial_puts(freq_str);
    serial_puts(")\n");
}

// Sets the frequency divisor for PIT Channel 0.
// Divisor 0 is interpreted by hardware as 65536.
void pit_set_divisor(uint16_t divisor) {
    // Command byte for Channel 0:
    // Bits 6-7: 00 = Channel 0
    // Bits 4-5: 11 = Access mode: lobyte/hibyte (send low byte, then high byte)
    // Bits 1-3: 010 = Mode 2 (Rate Generator)
    // Bit 0:    0 = 16-bit binary mode (not BCD)
    // Result: 00110100b = 0x34
    outb(PIT_COMMAND_REG, 0x34);
    io_wait(); // Short delay after command.
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF)); // Send low byte of divisor.
    io_wait();
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF)); // Send high byte of divisor.
    io_wait();
}

// PIT interrupt handler (C part). Called from IRQ0 assembly stub.
// The 'regs' parameter contains the state of registers at the time of the interrupt.
// It's passed by the common IRQ stub but might not be used by all simple handlers.
void pit_handler(registers_t *regs) {
    (void)regs; // regs is unused in this simple handler; (void)regs prevents compiler warning.
    system_ticks++; // Increment global system tick counter.

    // Call scheduler tick for process scheduling
    scheduler_tick();
    
    // Poll network for received packets every 10 ticks (~100ms at 100Hz)
    if (system_ticks % 10 == 0) {
        net_poll();
    }

    // Optional: Debugging message every N ticks (e.g., every second if PIT is 100Hz).
    /*
    if (system_ticks % 100 == 0) { // Assuming PIT_DEFAULT_DIVISOR targets 100 Hz
        if (itoa != NULL && serial_puts != NULL) { // Defensive check
            serial_puts("PIT tick! Uptime: ");
            char ticks_str[12];
            itoa(system_ticks / 100, ticks_str, 10); // Show seconds
            serial_puts(ticks_str);
            serial_puts("s (Total ticks: ");
            itoa(system_ticks, ticks_str, 10);
            serial_puts(ticks_str);
            serial_puts(")\n");
        } else {
            serial_puts("PIT tick! (count not printed - itoa unavailable)\n");
        }
    }
    */
}

// Get the current system tick count
uint32_t get_tick_count(void) {
    return system_ticks;
}
