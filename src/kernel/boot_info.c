/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/boot_info.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <stddef.h>
#include <serial.h> // Adjusted path to locate the header file
#include <multiboot.h> // Assuming you have a multiboot.h based on the spec
#include <util.h> // Adjusted path to locate the header file

char hex_digit(uint8_t val);

void print_boot_info(const multiboot_info_t *mbi) {
    serial_puts("\nBootloader Information:\n");
    serial_puts("  Multiboot Flags: 0x");
    serial_putc(hex_digit((mbi->flags >> 28) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 24) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 20) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 16) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 12) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 8) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 4) & 0xF));
    serial_putc(hex_digit(mbi->flags & 0xF));
    serial_puts("\n");

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        serial_puts("  Memory Map Address: 0x");
        serial_put_uint32(mbi->mmap_addr);
        serial_puts("\n");
        serial_puts("  Memory Map Length: 0x");
        serial_put_uint32(mbi->mmap_length);
        serial_puts("\n");
        // You would typically iterate through the memory map entries here
    }

    // Add more checks for other Multiboot flags as needed
}

// Helper function to print a hex digit
char hex_digit(uint8_t val) {
    if (val < 10) {
        return '0' + val;
    } else {
        return 'A' + (val - 10);
    }
}

// Helper function to print a 32-bit unsigned integer in hex
void serial_put_uint32(uint32_t n) {
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex_digit((n >> i) & 0xF));
    }
}
