/*
 * === AOS HEADER BEGIN ===
 * ./include/serial.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>


int serial_init();
void serial_putc(char c);
void serial_puts(const char *s);
void serial_put_uint32(uint32_t n);

#endif
