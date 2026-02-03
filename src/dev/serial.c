/*
 * === AOS HEADER BEGIN ===
 * ./src/dev/serial.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <stddef.h>

// Define the base address of the first serial port (COM1)
#define COM1_BASE 0x3F8

// Serial port registers (offsets from the base address)
#define SERIAL_DATA_PORT(base)      (base + 0)   // Data register (for read/write)
#define SERIAL_FIFO_COMMAND_PORT(base) (base + 2)   // FIFO control / Interrupt enable
#define SERIAL_LINE_CONTROL_PORT(base) (base + 3)   // Line control register
#define SERIAL_MODEM_CONTROL_PORT(base) (base + 4)   // Modem control register
#define SERIAL_LINE_STATUS_PORT(base) (base + 5)   // Line status register

// Helper function to write a byte to a serial port register
static inline void serial_outb(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %1, %0" : : "dN" (port), "a" (data));
}

// Helper function to read a byte from a serial port register
static inline uint8_t serial_inb(uint16_t port) {
    uint8_t data;
    __asm__ volatile ("inb %1, %0" : "=a" (data) : "dN" (port));
    return data;
}

// Initialize the serial port (COM1)
// Initialize the serial port (COM1)
int serial_init() {
    uint16_t base = COM1_BASE;

    // Disable all interrupts
    serial_outb(SERIAL_FIFO_COMMAND_PORT(base), 0x00);

    // Set the baud rate to 38400 (you can experiment with others)
    // Divisor = 115200 / baud_rate
    serial_outb(SERIAL_LINE_CONTROL_PORT(base), 0x80);    // Enable DLAB (set divisor latch access bit)
    serial_outb(SERIAL_DATA_PORT(base), 0x03);         // Divisor low byte (3)
    serial_outb(SERIAL_FIFO_COMMAND_PORT(base), 0x00);    // Divisor high byte (0)
    serial_outb(SERIAL_LINE_CONTROL_PORT(base), 0x03);    // 8 bits, no parity, one stop bit **(DLAB is now 0)**

    // Enable FIFO, clear buffers, 14-byte threshold
    serial_outb(SERIAL_FIFO_COMMAND_PORT(base), 0xC7);

    // Enable IRQs (you'll handle these later)
    serial_outb(SERIAL_MODEM_CONTROL_PORT(base), 0x0B);

    return 0; // Initialization successful
}
// Check if the transmit FIFO is empty
static int serial_is_transmit_fifo_empty(uint16_t port) {
    return serial_inb(SERIAL_LINE_STATUS_PORT(port)) & 0x20;
}

// Send a single character to the serial port
void serial_putc(char c) {
    uint16_t base = COM1_BASE;
    while (serial_is_transmit_fifo_empty(base) == 0); // Wait for FIFO to be empty
    serial_outb(SERIAL_DATA_PORT(base), c);
}

// Send a null-terminated string to the serial port
void serial_puts(const char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        serial_putc(s[i]);
    }
}
