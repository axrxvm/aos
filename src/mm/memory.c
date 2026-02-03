/*
 * === AOS HEADER BEGIN ===
 * ./src/mm/memory.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <stddef.h>
#include <serial.h> // Adjusted path to locate the header file
#include <stdlib.h> // For itoa
#include <multiboot.h> // For proper multiboot structure

void print_memory_info(const multiboot_info_t *mbi) {
    char buf[16];
    
    serial_puts("Memory Information:\n");
    if (mbi->flags & 1) {
        serial_puts("  Lower memory (below 1MB): ");
        itoa(mbi->mem_lower, buf, 10);
        serial_puts(buf);
        serial_puts(" KB\n");
        
        serial_puts("  Upper memory (above 1MB): ");
        itoa(mbi->mem_upper, buf, 10);
        serial_puts(buf);
        serial_puts(" KB\n");
        
        uint32_t total_memory_kb = mbi->mem_lower + mbi->mem_upper;
        serial_puts("  Total memory: ");
        itoa(total_memory_kb / 1024, buf, 10);
        serial_puts(buf);
        serial_puts(" MB (");
        itoa(total_memory_kb, buf, 10);
        serial_puts(buf);
        serial_puts(" KB)\n");
    } else {
        serial_puts("  Memory information not provided by bootloader.\n");
    }
}
