/*
 * === AOS HEADER BEGIN ===
 * ./src/dev/io.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


// src/io.c
#include <stdint.h>

// Inline assembly for outputting a byte to a port
void outb(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %0, %1" : : "a" (data), "dN" (port));
}

// Inline assembly for reading a byte from a port
uint8_t inb(uint16_t port) {
    uint8_t data;
    __asm__ volatile ("inb %1, %0" : "=a" (data) : "dN" (port));
    return data;
}

void io_wait(void) {
    asm volatile ("outb %%al, $0x80" : : "a"(0));
}

void outw(uint16_t port, uint16_t value) {
    asm volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

uint16_t inw(uint16_t port) {
    uint16_t data;
    asm volatile ("inw %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}

void outl(uint16_t port, uint32_t value) {
    asm volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t inl(uint16_t port) {
    uint32_t data;
    asm volatile ("inl %1, %0" : "=a"(data) : "Nd"(port));
    return data;
}
