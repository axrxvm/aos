/*
 * === AOS HEADER BEGIN ===
 * src/kernel/debug.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <debug.h>
#include <krm.h>
#include <vga.h>
#include <serial.h>
#include <stdlib.h> // For itoa
// Removed <string.h> as no string functions are used (last time i checked)

#ifndef VGA_COLOR_WHITE_ON_RED // Ensure this is defined, e.g. in vga.h or here
#define VGA_COLOR_WHITE_ON_RED 0xCF // Bright White text on Red background
#endif

void print_backtrace(uint32_t max_frames) {
    vga_puts("\nStack Backtrace:\n");
    serial_puts("\nStack Backtrace:\n");

    uint32_t *ebp;
    asm volatile ("mov %%ebp, %0" : "=r"(ebp));

    char buf[18]; // "  0x" + 8 hex digits + "\n" + null = 15 chars, 18 is safe

    for (uint32_t frame = 0; ebp && frame < max_frames; ++frame) {
        // Ensure ebp is readable and points to a valid stack region if possible.
        // This basic check doesn't validate the region but checks for NULL.
        if (ebp < (uint32_t*)0x1000) { // Very basic sanity check, assumes kernel stack is higher
            vga_puts("  (EBP seems invalid or too low: 0x");
            itoa((uint32_t)ebp, buf, 10); vga_puts(buf); vga_puts(")\n");
            serial_puts("  (EBP seems invalid or too low: 0x");
            itoa((uint32_t)ebp, buf, 10); serial_puts(buf); serial_puts(")\n");
            break;
        }

        uint32_t eip = ebp[1]; // Return address is [ebp+4]
        if (eip == 0) {
             vga_puts("  (Null EIP, end of trace?)\n");
             serial_puts("  (Null EIP, end of trace?)\n");
             break;
        }

        vga_puts("  0x"); itoa(eip, buf, 10); vga_puts(buf); vga_puts("\n");
        serial_puts("  0x"); itoa(eip, buf, 10); serial_puts(buf); serial_puts("\n");

        uint32_t prev_ebp_val = ebp[0];
        if (prev_ebp_val == (uint32_t)ebp) { // Basic check for stack corruption or end of chain like ebp: ebp
            vga_puts("  (Loop or end of chain: next EBP is current EBP: 0x");
            itoa((uint32_t)ebp, buf, 10); vga_puts(buf); vga_puts(")\n");
            serial_puts("  (Loop or end of chain: next EBP is current EBP: 0x");
            itoa((uint32_t)ebp, buf, 10); serial_puts(buf); serial_puts(")\n");
            break;
        }
        if (prev_ebp_val < (uint32_t)ebp && frame > 0) { // Stack should grow downwards, so prev_ebp should be higher
            vga_puts("  (Warning: next EBP 0x"); itoa(prev_ebp_val, buf, 10); vga_puts(buf);
            vga_puts(" is lower than current EBP 0x"); itoa((uint32_t)ebp, buf, 10); vga_puts(buf); vga_puts(")\n");
            serial_puts("  (Warning: next EBP is lower than current EBP)\n");
            // allow it to proceed, might be a special case or just one frame wrong.
        }
        ebp = (uint32_t *)prev_ebp_val; // Go to the previous stack frame: ebp = [ebp]
    }
    vga_puts("End of Backtrace.\n");
    serial_puts("End of Backtrace.\n");
}

void panic_screen(registers_t *regs, const char *message, const char *file, uint32_t line) {
    // CRITICAL: Prevent cascading panics
    // Use a simple static guard that doesn't depend on any other system
    static volatile uint8_t panic_guard = 0;
    
    asm volatile ("cli"); // Disable interrupts
    
    // Check if we're already in a panic
    if (panic_guard) {
        // Double panic! Go directly to minimal halt without trying anything fancy
        // Direct VGA write - no function calls
        uint16_t* vga = (uint16_t*)0xB8000;
        const char* msg = "!!! DOUBLE PANIC - HALT !!!";
        for (int i = 0; i < 80 * 25; i++) {
            vga[i] = ' ' | (0xCF << 8); // White on red
        }
        for (int i = 0; msg[i]; i++) {
            vga[12 * 80 + 26 + i] = msg[i] | (0xCF << 8);
        }
        while(1) asm volatile("hlt");
    }
    
    panic_guard = 1; // Set guard before doing anything else

    // Send panic info to serial for debugging (before entering KRM)
    serial_puts("\n!!! KERNEL PANIC !!!\n");
    serial_puts("Message: "); serial_puts(message ? message : "(null)"); serial_puts("\n");
    serial_puts("Location: "); serial_puts(file); serial_puts(":");
    char num_buf[12];
    itoa(line, num_buf, 10);
    serial_puts(num_buf); serial_puts("\n");

    if (regs) {
        char reg_buf[12];
        serial_puts("\nRegisters:\n");
        
        #define PRINT_REG_SERIAL(reg_name_str, reg_val_local, buf_local) \
            serial_puts(reg_name_str); itoa(reg_val_local, buf_local, 10); serial_puts(buf_local); serial_puts("  ");

        PRINT_REG_SERIAL("EAX: 0x", regs->eax, reg_buf);
        PRINT_REG_SERIAL("EBX: 0x", regs->ebx, reg_buf);
        PRINT_REG_SERIAL("ECX: 0x", regs->ecx, reg_buf);
        PRINT_REG_SERIAL("EDX: 0x", regs->edx, reg_buf);
        serial_puts("\n");

        PRINT_REG_SERIAL("ESI: 0x", regs->esi, reg_buf);
        PRINT_REG_SERIAL("EDI: 0x", regs->edi, reg_buf);
        PRINT_REG_SERIAL("EBP: 0x", regs->ebp, reg_buf);

        uint32_t current_esp = regs->esp_dummy;
        if ((regs->cs & 0x3) != 0) {
            current_esp = regs->useresp;
        }
        PRINT_REG_SERIAL("ESP: 0x", current_esp, reg_buf);
        serial_puts("\n");

        PRINT_REG_SERIAL("EIP: 0x", regs->eip, reg_buf);
        PRINT_REG_SERIAL("CS:  0x", regs->cs, reg_buf);
        PRINT_REG_SERIAL("DS:  0x", regs->ds, reg_buf);
        serial_puts("\n");

        PRINT_REG_SERIAL("EFLAGS: 0x", regs->eflags, reg_buf);
        serial_puts("\n");

        if (regs->int_no == 8 || (regs->int_no >= 10 && regs->int_no <= 14) || 
            regs->int_no == 17 || regs->int_no == 30) {
            PRINT_REG_SERIAL("Error Code: 0x", regs->err_code, reg_buf);
            serial_puts("\n");
        }
        PRINT_REG_SERIAL("Interrupt: 0x", regs->int_no, reg_buf);
        serial_puts("\n\n");

        #undef PRINT_REG_SERIAL
    }

    // Skip unsafe backtrace - KRM will do its own safe backtrace collection
    serial_puts("\nEntering Kernel Recovery Mode (KRM)...\n");

    // ENTER KERNEL RECOVERY MODE - THIS DOES NOT RETURN
    krm_enter(regs, message, file, line);

    // Should never reach here
    asm volatile ("hlt");
}

void panic_msg_loc(const char *message, const char *file, uint32_t line) {
    // Software panic - no register state available
    // Enter KRM directly (does not return)
    krm_enter(NULL, message, file, line);
    
    // Should never reach here
    asm volatile ("cli");
    asm volatile ("hlt");
}
