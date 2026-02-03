/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/debug.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <debug.h>
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
    asm volatile ("cli"); // Disable interrupts

    // Assuming vga_set_color, vga_clear, vga_puts are always available if linked.
    vga_set_color(VGA_COLOR_WHITE_ON_RED);
    vga_clear();
    vga_puts("!!! KERNEL PANIC !!!\n\n");

    // Assuming serial_puts is always available.
    serial_puts("!!! KERNEL PANIC !!!\n\n");

    vga_puts("Message: "); vga_puts(message ? message : "(null)"); vga_puts("\n");
    serial_puts("Message: "); serial_puts(message ? message : "(null)"); serial_puts("\n");

    char num_buf[12];  // Buffer for itoa line number
    // Assuming itoa is always available if linked.
    // And file is expected to be a valid string literal from __FILE__.
    itoa(line, num_buf, 10);
    vga_puts("Location: "); vga_puts(file); vga_puts(":"); vga_puts(num_buf); vga_puts("\n\n");
    serial_puts("Location: "); serial_puts(file); serial_puts(":"); serial_puts(num_buf); serial_puts("\n\n");


    if (regs) { // itoa, serial_puts, vga_puts are assumed to be available.
        char reg_buf[12]; // For itoa register values

        // Define separate macros for VGA and Serial output to simplify logic
        #define PRINT_REG_VGA(reg_name_str, reg_val_local, buf_local) \
            vga_puts(reg_name_str); itoa(reg_val_local, buf_local, 10); vga_puts(buf_local); vga_puts("  ");

        #define PRINT_REG_SERIAL(reg_name_str, reg_val_local, buf_local) \
            serial_puts(reg_name_str); itoa(reg_val_local, buf_local, 10); serial_puts(buf_local); serial_puts("  ");

        vga_puts("Registers:\n");
        serial_puts("Registers:\n");

        PRINT_REG_VGA("EAX: 0x", regs->eax, reg_buf); PRINT_REG_SERIAL("EAX: 0x", regs->eax, reg_buf);
        PRINT_REG_VGA("EBX: 0x", regs->ebx, reg_buf); PRINT_REG_SERIAL("EBX: 0x", regs->ebx, reg_buf);
        PRINT_REG_VGA("ECX: 0x", regs->ecx, reg_buf); PRINT_REG_SERIAL("ECX: 0x", regs->ecx, reg_buf);
        PRINT_REG_VGA("EDX: 0x", regs->edx, reg_buf); PRINT_REG_SERIAL("EDX: 0x", regs->edx, reg_buf);
        vga_puts("\n"); serial_puts("\n");

        PRINT_REG_VGA("ESI: 0x", regs->esi, reg_buf); PRINT_REG_SERIAL("ESI: 0x", regs->esi, reg_buf);
        PRINT_REG_VGA("EDI: 0x", regs->edi, reg_buf); PRINT_REG_SERIAL("EDI: 0x", regs->edi, reg_buf);
        PRINT_REG_VGA("EBP: 0x", regs->ebp, reg_buf); PRINT_REG_SERIAL("EBP: 0x", regs->ebp, reg_buf);

        uint32_t current_esp = regs->esp_dummy;
        if ((regs->cs & 0x3) != 0) { // Check CPL from CS
            current_esp = regs->useresp;
        }
        PRINT_REG_VGA("ESP: 0x", current_esp, reg_buf); PRINT_REG_SERIAL("ESP: 0x", current_esp, reg_buf);
        vga_puts("\n"); serial_puts("\n");

        PRINT_REG_VGA("EIP: 0x", regs->eip, reg_buf); PRINT_REG_SERIAL("EIP: 0x", regs->eip, reg_buf);
        PRINT_REG_VGA("CS:  0x", regs->cs, reg_buf); PRINT_REG_SERIAL("CS:  0x", regs->cs, reg_buf);
        PRINT_REG_VGA("DS:  0x", regs->ds, reg_buf); PRINT_REG_SERIAL("DS:  0x", regs->ds, reg_buf);
        vga_puts("\n"); serial_puts("\n");

        PRINT_REG_VGA("EFLAGS: 0x", regs->eflags, reg_buf); PRINT_REG_SERIAL("EFLAGS: 0x", regs->eflags, reg_buf);
        vga_puts("\n"); serial_puts("\n");

        if (regs->int_no == 8 || (regs->int_no >= 10 && regs->int_no <= 14) || regs->int_no == 17 || regs->int_no == 30) {
            PRINT_REG_VGA("Error Code: 0x", regs->err_code, reg_buf); PRINT_REG_SERIAL("Error Code: 0x", regs->err_code, reg_buf);
            vga_puts("\n"); serial_puts("\n");
        }
        PRINT_REG_VGA("Interrupt: 0x", regs->int_no, reg_buf); PRINT_REG_SERIAL("Interrupt: 0x", regs->int_no, reg_buf);
        vga_puts("\n\n"); serial_puts("\n\n");

        // Undefine macros after use to keep them local
        #undef PRINT_REG_VGA
        #undef PRINT_REG_SERIAL

    } else {
        vga_puts("Register state not available.\n\n");
        serial_puts("Register state not available.\n\n");
    }

    // Assuming print_backtrace is always available if linked.
    print_backtrace(10);

    vga_puts("\nSystem Halted. Please reboot.");
    serial_puts("\nSystem Halted. Please reboot.\n");

    asm volatile ("hlt");
}

void panic_msg_loc(const char *message, const char *file, uint32_t line) {
    // Assuming panic_screen is always available if linked.
    // No valid 'registers_t' struct is available here as it's a software panic.
    panic_screen(NULL, message, file, line);
    // Fallback removed as panic_screen itself will halt. If panic_screen is not linked,
    // it's a build issue. If it were to return (it shouldn't), the hlt in panic_screen handles it.
}
