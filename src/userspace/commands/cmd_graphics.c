/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_graphics.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.8
 * === AOS HEADER END ===
 */

#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <keyboard.h>
#include <process.h>

extern void kprint(const char *str);

// List available graphics modes
static void cmd_listmodes(const char* args) {
    (void)args;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== Available Video Modes ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprint("Text Modes:");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    kprint("  0x03  - 80x25 Text Mode (16 colors)");
    kprint("");
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprint("Legacy Graphics:");
    vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    kprint("  0x13  - 320x200 Graphics (256 colors)");
    kprint("");
    
    if (vga_detect_vbe()) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("VBE Graphics Modes:");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        kprint("  0x101 - 640x480x256");
        kprint("  0x103 - 800x600x256");
        kprint("  0x105 - 1024x768x256");
        kprint("  0x112 - 640x480x16M (24-bit)");
        kprint("  0x115 - 800x600x16M (24-bit)");
        kprint("  0x118 - 1024x768x16M (24-bit)");
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        kprint("Note: VBE modes not available");
        vga_set_color(VGA_ATTR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    }
    
    kprint("");
    vga_set_color(VGA_ATTR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    kprint("Use 'setmode <mode>' to switch modes");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

// Set video mode
static void cmd_setmode(const char* args) {
    if (!args || !*args) {
        kprint("Usage: setmode <mode>");
        kprint("Examples:");
        kprint("  setmode 0x03     - Text mode 80x25");
        kprint("  setmode 0x13     - Graphics 320x200x256");
        kprint("  setmode 0x101    - Graphics 640x480");
        kprint("  setmode 0x103    - Graphics 800x600");
        kprint("  setmode 0x105    - Graphics 1024x768");
        return;
    }
    
    // Parse mode number (hex or decimal)
    uint16_t mode = 0;
    const char* p = args;
    
    // Skip whitespace
    while (*p == ' ' || *p == '\t') p++;
    
    // Check for hex prefix
    if (*p == '0' && (*(p+1) == 'x' || *(p+1) == 'X')) {
        p += 2;
        // Parse hex
        while (*p) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                mode = (mode << 4) | (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                mode = (mode << 4) | (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                mode = (mode << 4) | (c - 'A' + 10);
            } else {
                break;
            }
            p++;
        }
    } else {
        // Parse decimal
        while (*p >= '0' && *p <= '9') {
            mode = mode * 10 + (*p - '0');
            p++;
        }
    }
    
    if (mode == 0) {
        kprint("Error: Invalid mode number");
        return;
    }
    
    kprint("Setting video mode...");
    if (vga_set_mode(mode)) {
        if (mode == 0x03) {
            vga_clear();
            kprint("Video mode set successfully");
            kprint("Returned to text mode 80x25");
        } else {
            serial_puts("Video mode set successfully\n");
            serial_puts("Note: Display is now in graphics mode\n");
            serial_puts("Use 'setmode 0x03' to return to text mode\n");
        }
    } else {
        kprint("Error: Failed to set video mode");
    }
}

// Hex color demo
static void cmd_hexdemo(const char* args) {
    (void)args;
    
    kprint("=== Hex Color Demo ===");
    kprint("Converting hex colors to RGB...");
    kprint("");
    
    // Test various hex colors
    const char* colors[] = {
        "#FF0000", "#00FF00", "#0000FF",
        "#FFFF00", "#FF00FF", "#00FFFF",
        "#FFFFFF", "#000000", "#808080"
    };
    const char* names[] = {
        "Red", "Green", "Blue",
        "Yellow", "Magenta", "Cyan",
        "White", "Black", "Gray"
    };
    
    for (int i = 0; i < 9; i++) {
        rgb_color_t rgb = vga_hex_to_rgb(colors[i]);
        
        char line[128];
        char r_str[4], g_str[4], b_str[4];
        itoa(rgb.r, r_str, 10);
        itoa(rgb.g, g_str, 10);
        itoa(rgb.b, b_str, 10);
        
        strcpy(line, colors[i]);
        strcat(line, " (");
        strcat(line, names[i]);
        strcat(line, ") -> RGB(");
        strcat(line, r_str);
        strcat(line, ", ");
        strcat(line, g_str);
        strcat(line, ", ");
        strcat(line, b_str);
        strcat(line, ")");
        
        kprint(line);
    }
    
    kprint("");
    kprint("Hex color support is working!");
}

// Graphics demo - draw some shapes
static void cmd_gfxdemo(const char* args) {
    (void)args;
    
    kprint("=== Graphics Demo ===");
    kprint("Switching to 320x200 graphics mode...");
    serial_puts("Starting graphics demo\n");
    
    // Switch to 320x200x256 graphics mode
    if (!vga_set_mode(VGA_MODE_320x200x256)) {
        serial_puts("ERROR: Failed to switch to graphics mode\n");
        kprint("Error: Failed to switch to graphics mode");
        return;
    }
    
    serial_puts("Graphics mode active, drawing shapes...\n");
    
    // Clear screen to black - simplified
    for (uint16_t y = 0; y < 200; y++) {
        for (uint16_t x = 0; x < 320; x++) {
            vga_plot_pixel(x, y, 0);
        }
    }
    
    serial_puts("Drawing red rectangle...\n");
    // Draw red rectangle
    for (uint16_t y = 20; y < 60; y++) {
        for (uint16_t x = 20; x < 100; x++) {
            vga_plot_pixel(x, y, 4);  // Red
        }
    }
    
    serial_puts("Drawing green circle...\n");
    vga_draw_circle(160, 100, 40, 2);  // Green
    
    serial_puts("Drawing blue line...\n");
    vga_draw_line(120, 150, 200, 180, 1);  // Blue
    
    serial_puts("Drawing yellow triangle...\n");
    vga_draw_triangle(250, 30, 220, 80, 280, 80, 14);  // Yellow
    
    serial_puts("\n=== All shapes drawn! Press 'x' to return ===\n");
    
    // Thoroughly clear keyboard buffer - read multiple times to be sure
    serial_puts("Clearing keyboard buffer...\n");
    for (int i = 0; i < 10; i++) {
        keyboard_get_scancode();
    }
    
    // Wait a moment for buffer to settle
    for (volatile int i = 0; i < 100000; i++) { }
    
    serial_puts("Waiting for 'x' key...\n");
    
    // Wait for 'x' key specifically (scancode 0x2D)
    uint8_t scancode;
    int x_detected = 0;
    
    while (!x_detected) {
        scancode = keyboard_get_scancode();
        
        if (scancode == 0x2D) {  // 'x' key scancode
            x_detected = 1;
            serial_puts("'x' key detected, returning to text mode...\n");
        }
    }
    
    // Return to text mode (vga_set_mode will clean the buffer)
    if (vga_set_mode(0x03)) {
        // Force re-init VGA subsystem
        vga_init();
        
        // Triple clear to be absolutely sure
        vga_clear();
        vga_clear();
        vga_clear();
        
        // Set position to top of screen
        vga_set_position(0, 0);
        
        // Print success message
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("=================================");
        kprint("    Graphics Demo Completed!");
        kprint("=================================");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kprint("");
        kprint("Shapes displayed:");
        kprint("  * Red rectangle (top left)");
        kprint("  * Green circle (center)");
        kprint("  * Blue line (bottom center)");
        kprint("  * Yellow triangle (top right)");
        kprint("");
        kprint("Exited with 'x' key");
        kprint("");
        serial_puts("Successfully returned to text mode\n");
    } else {
        serial_puts("ERROR: Failed to return to text mode\n");
    }
}

// Color gradient demo in text mode
static void cmd_gradient(const char* args) {
    (void)args;
    
    kprint("=== VGA Color Gradient Demo ===");
    kprint("");
    
    // Display color gradient using VGA text mode colors
    for (int i = 0; i < 16; i++) {
        char line[80];
        char num_str[4];
        
        vga_set_color(VGA_ATTR(i, VGA_COLOR_BLACK));
        
        strcpy(line, "Color ");
        itoa(i, num_str, 10);
        strcat(line, num_str);
        strcat(line, ": ");
        
        // Add color bars
        for (int j = 0; j < 20; j++) {
            strcat(line, "#");
        }
        
        vga_puts(line);
        vga_puts("\n");
    }
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    kprint("16 VGA colors displayed!");
}

// Display graphics capabilities
static void cmd_gfxinfo(const char* args) {
    (void)args;
    
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprint("=== VGA Graphics Capabilities ===");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprint("");
    
    // Check VBE support
    if (vga_detect_vbe()) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("[OK] VBE 2.0+ Support: Enabled");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprint("[WARN] VBE Support: Not Available");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    kprint("");
    kprint("Supported Features:");
    kprint("  * Text modes: 80x25, 80x50, 90x30, 90x60, 40x25");
    kprint("  * Graphics modes: 320x200, 640x480, 800x600, 1024x768");
    kprint("  * Color formats: RGB24, RGBA32, RGB565, RGB555, Hex (#RRGGBB)");
    kprint("  * Drawing primitives: Pixels, Lines, Circles, Ellipses, Rectangles");
    kprint("  * Advanced: Triangles, Polygons, Bitmaps with alpha");
    kprint("  * Effects: Filters, Blending, Double buffering");
    kprint("");
    
    // Current mode info
    vga_mode_info_t* info = vga_get_mode_info();
    
    kprint("Current Mode:");
    
    char line[80];
    char num_str[8];
    
    strcpy(line, "  Mode: 0x");
    itoa(info->mode_number, num_str, 16);
    strcat(line, num_str);
    kprint(line);
    
    if (info->type == VGA_MODE_TEXT) {
        strcpy(line, "  Type: Text ");
    } else {
        strcpy(line, "  Type: Graphics ");
    }
    itoa(info->width, num_str, 10);
    strcat(line, num_str);
    strcat(line, "x");
    itoa(info->height, num_str, 10);
    strcat(line, num_str);
    kprint(line);
    
    strcpy(line, "  Bits per pixel: ");
    itoa(info->bpp, num_str, 10);
    strcat(line, num_str);
    kprint(line);
}

// Register graphics commands
void cmd_module_graphics_register(void) {
    command_register_with_category(
        "listmodes",
        "",
        "List available video modes",
        "Graphics",
        cmd_listmodes
    );
    
    command_register_with_category(
        "setmode",
        "<mode>",
        "Set video mode (0x03=text, 0x13=320x200, etc)",
        "Graphics",
        cmd_setmode
    );
    
    command_register_with_category(
        "gfxinfo",
        "",
        "Display graphics capabilities and current mode",
        "Graphics",
        cmd_gfxinfo
    );
    
    command_register_with_category(
        "gfxdemo",
        "",
        "Graphics drawing demonstration",
        "Graphics",
        cmd_gfxdemo
    );
    
    command_register_with_category(
        "hexdemo",
        "",
        "Hex color conversion demonstration",
        "Graphics",
        cmd_hexdemo
    );
    
    command_register_with_category(
        "gradient",
        "",
        "Display VGA color gradient in text mode",
        "Graphics",
        cmd_gradient
    );
}
