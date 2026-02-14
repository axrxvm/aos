/*
 * === AOS HEADER BEGIN ===
 * src/kernel/boot_info.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <stddef.h>
#include <serial.h> // Adjusted path to locate the header file
#include <multiboot.h> // Assuming you have a multiboot.h based on the spec
#include <util.h> // Adjusted path to locate the header file

char hex_digit(uint8_t val);

void print_boot_info(const multiboot_info_t *mbi) {
    serial_puts("\n=== Multiboot Information ===\n");
    serial_puts("  Flags: 0x");
    serial_putc(hex_digit((mbi->flags >> 28) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 24) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 20) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 16) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 12) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 8) & 0xF));
    serial_putc(hex_digit((mbi->flags >> 4) & 0xF));
    serial_putc(hex_digit(mbi->flags & 0xF));
    serial_puts("\n\n");

    // Memory information (flags bit 0)
    if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        serial_puts("Memory:\n");
        serial_puts("  Lower: ");
        serial_put_uint32(mbi->mem_lower);
        serial_puts(" KB\n");
        serial_puts("  Upper: ");
        serial_put_uint32(mbi->mem_upper);
        serial_puts(" KB\n");
    }

    // Boot device (flags bit 1)
    if (mbi->flags & MULTIBOOT_INFO_BOOTDEV) {
        serial_puts("Boot Device: 0x");
        serial_put_uint32(mbi->boot_device);
        serial_puts("\n");
    }

    // Command line (flags bit 2)
    if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
        serial_puts("Command Line: ");
        if (mbi->cmdline) {
            serial_puts((char*)(uint32_t)mbi->cmdline);
        }
        serial_puts("\n");
    }

    // Bootloader name (flags bit 9)
    if (mbi->flags & MULTIBOOT_INFO_BOOT_LOADER_NAME) {
        serial_puts("Bootloader: ");
        if (mbi->boot_loader_name) {
            serial_puts((char*)(uint32_t)mbi->boot_loader_name);
        } else {
            serial_puts("(unknown)");
        }
        serial_puts("\n");
    }

    // Modules (flags bit 3)
    if (mbi->flags & MULTIBOOT_INFO_MODS) {
        serial_puts("Modules: ");
        serial_put_uint32(mbi->mods_count);
        serial_puts(" loaded\n");
        if (mbi->mods_count > 0 && mbi->mods_addr) {
            multiboot_module_t* mods = (multiboot_module_t*)(uint32_t)mbi->mods_addr;
            for (uint32_t i = 0; i < mbi->mods_count; i++) {
                serial_puts("  [");
                serial_putc('0' + i);
                serial_puts("] 0x");
                serial_put_uint32(mods[i].mod_start);
                serial_puts(" - 0x");
                serial_put_uint32(mods[i].mod_end);
                if (mods[i].cmdline) {
                    serial_puts(" (");
                    serial_puts((char*)(uint32_t)mods[i].cmdline);
                    serial_puts(")");
                }
                serial_puts("\n");
            }
        }
    }

    // Memory map (flags bit 6)
    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        serial_puts("Memory Map:\n");
        serial_puts("  Address: 0x");
        serial_put_uint32(mbi->mmap_addr);
        serial_puts("\n  Length: 0x");
        serial_put_uint32(mbi->mmap_length);
        serial_puts("\n");
    }

    // VBE information (flags bit 11)
    if (mbi->flags & MULTIBOOT_INFO_VBE_INFO) {
        serial_puts("\nVBE Information:\n");
        serial_puts("  Control Info: 0x");
        serial_put_uint32(mbi->vbe_control_info);
        serial_puts("\n  Mode Info: 0x");
        serial_put_uint32(mbi->vbe_mode_info);
        serial_puts("\n  Current Mode: 0x");
        serial_putc(hex_digit((mbi->vbe_mode >> 12) & 0xF));
        serial_putc(hex_digit((mbi->vbe_mode >> 8) & 0xF));
        serial_putc(hex_digit((mbi->vbe_mode >> 4) & 0xF));
        serial_putc(hex_digit(mbi->vbe_mode & 0xF));
        serial_puts("\n  Interface: seg=0x");
        serial_putc(hex_digit((mbi->vbe_interface_seg >> 12) & 0xF));
        serial_putc(hex_digit((mbi->vbe_interface_seg >> 8) & 0xF));
        serial_putc(hex_digit((mbi->vbe_interface_seg >> 4) & 0xF));
        serial_putc(hex_digit(mbi->vbe_interface_seg & 0xF));
        serial_puts(", off=0x");
        serial_putc(hex_digit((mbi->vbe_interface_off >> 12) & 0xF));
        serial_putc(hex_digit((mbi->vbe_interface_off >> 8) & 0xF));
        serial_putc(hex_digit((mbi->vbe_interface_off >> 4) & 0xF));
        serial_putc(hex_digit(mbi->vbe_interface_off & 0xF));
        serial_puts(", len=0x");
        serial_putc(hex_digit((mbi->vbe_interface_len >> 12) & 0xF));
        serial_putc(hex_digit((mbi->vbe_interface_len >> 8) & 0xF));
        serial_putc(hex_digit((mbi->vbe_interface_len >> 4) & 0xF));
        serial_putc(hex_digit(mbi->vbe_interface_len & 0xF));
        serial_puts("\n");
    }

    // Framebuffer information (flags bit 12)
    if (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        serial_puts("\nFramebuffer Information:\n");
        serial_puts("  Address: 0x");
        serial_put_uint32((uint32_t)mbi->framebuffer_addr);
        serial_puts("\n  Pitch: ");
        serial_put_uint32(mbi->framebuffer_pitch);
        serial_puts(" bytes\n  Resolution: ");
        serial_put_uint32(mbi->framebuffer_width);
        serial_puts("x");
        serial_put_uint32(mbi->framebuffer_height);
        serial_puts("\n  BPP: ");
        serial_putc('0' + (mbi->framebuffer_bpp / 10));
        serial_putc('0' + (mbi->framebuffer_bpp % 10));
        serial_puts("\n  Type: ");
        switch (mbi->framebuffer_type) {
            case MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED:
                serial_puts("Indexed (palette)\n");
                break;
            case MULTIBOOT_FRAMEBUFFER_TYPE_RGB:
                serial_puts("RGB\n");
                serial_puts("    Red:   pos=");
                serial_putc('0' + (mbi->rgb.framebuffer_red_field_position / 10));
                serial_putc('0' + (mbi->rgb.framebuffer_red_field_position % 10));
                serial_puts(", size=");
                serial_putc('0' + (mbi->rgb.framebuffer_red_mask_size / 10));
                serial_putc('0' + (mbi->rgb.framebuffer_red_mask_size % 10));
                serial_puts("\n    Green: pos=");
                serial_putc('0' + (mbi->rgb.framebuffer_green_field_position / 10));
                serial_putc('0' + (mbi->rgb.framebuffer_green_field_position % 10));
                serial_puts(", size=");
                serial_putc('0' + (mbi->rgb.framebuffer_green_mask_size / 10));
                serial_putc('0' + (mbi->rgb.framebuffer_green_mask_size % 10));
                serial_puts("\n    Blue:  pos=");
                serial_putc('0' + (mbi->rgb.framebuffer_blue_field_position / 10));
                serial_putc('0' + (mbi->rgb.framebuffer_blue_field_position % 10));
                serial_puts(", size=");
                serial_putc('0' + (mbi->rgb.framebuffer_blue_mask_size / 10));
                serial_putc('0' + (mbi->rgb.framebuffer_blue_mask_size % 10));
                serial_puts("\n");
                break;
            case MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT:
                serial_puts("EGA Text\n");
                break;
            default:
                serial_puts("Unknown\n");
        }
    }

    serial_puts("=== End Multiboot Information ===\n\n");
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
