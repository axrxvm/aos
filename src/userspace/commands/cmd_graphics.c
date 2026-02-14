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

// Simple pseudo-random number generator
static unsigned int prng_seed = 1;
static int simple_rand(void) {
    prng_seed = (prng_seed * 1103515245 + 12345) & 0x7fffffff;
    return prng_seed;
}

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

// Bouncing ball animation
static void cmd_bouncy(const char* args) {
    (void)args;
    
    kprint("=== Bouncing Ball Demo ===");
    kprint("Switching to 320x200 mode...");
    
    if (!vga_set_mode(VGA_MODE_320x200x256)) {
        kprint("Error: Failed to switch to graphics mode");
        return;
    }
    
    serial_puts("Bouncing ball animation started. Press 'q' to quit.\n");
    
    // Ball properties - use fixed-point (multiply by 16 for sub-pixel precision)
    int ball_x = 160 << 4;  // 160 * 16 = 2560
    int ball_y = 100 << 4;  // 100 * 16 = 1600
    int vel_x = 6;          // 0.375 pixels per frame (6/16)
    int vel_y = 5;          // 0.3125 pixels per frame (5/16)
    int radius = 8;
    int color = 12;  // Yellow
    
    // Clear to black
    for (uint16_t y = 0; y < 200; y++) {
        for (uint16_t x = 0; x < 320; x++) {
            vga_plot_pixel(x, y, 0);
        }
    }
    
    // Clear keyboard
    for (int i = 0; i < 10; i++) {
        keyboard_get_scancode();
    }
    
    int running = 1;
    int frame = 0;
    int last_draw_x = 160, last_draw_y = 100;
    
    while (running) {
        // Get current integer position from fixed-point
        int draw_x = ball_x >> 4;
        int draw_y = ball_y >> 4;
        
        // Only redraw if position changed
        if (draw_x != last_draw_x || draw_y != last_draw_y) {
            // Clear previous ball
            vga_fill_circle(last_draw_x, last_draw_y, radius, 0);
        }
        
        // Update position with fixed-point arithmetic
        ball_x += vel_x;
        ball_y += vel_y;
        
        // Bounce off walls (using fixed-point boundary checks)
        int left_bound = radius << 4;
        int right_bound = (320 - radius) << 4;
        int top_bound = radius << 4;
        int bottom_bound = (200 - radius) << 4;
        
        if (ball_x <= left_bound || ball_x >= right_bound) {
            vel_x = -vel_x;
            ball_x += vel_x;  // Adjust to prevent getting stuck
        }
        if (ball_y <= top_bound || ball_y >= bottom_bound) {
            vel_y = -vel_y;
            ball_y += vel_y;  // Adjust to prevent getting stuck
        }
        
        // Change color occasionally
        if (frame % 100 == 0) {
            color = (color + 1) % 16;
            if (color == 0) color = 1;
        }
        
        // Update integer position for next frame
        draw_x = ball_x >> 4;
        draw_y = ball_y >> 4;
        
        // Draw new ball
        vga_fill_circle(draw_x, draw_y, radius, color);
        last_draw_x = draw_x;
        last_draw_y = draw_y;
        
        // Check for 'q' key
        uint8_t scan = keyboard_get_scancode();
        if (scan == 0x10) {  // 'q' key
            running = 0;
        }
        
        // Shorter delay for smoother animation
        for (volatile int i = 0; i < 25000; i++) { }
        frame++;
    }
    
    // Return to text mode
    if (vga_set_mode(0x03)) {
        vga_init();
        vga_clear();
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("Bouncing ball demo ended!");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

// Starfield screensaver effect
static void cmd_starfield(const char* args) {
    (void)args;
    
    kprint("=== Starfield Screensaver ===");
    kprint("Switching to 320x200 mode...");
    
    if (!vga_set_mode(VGA_MODE_320x200x256)) {
        kprint("Error: Failed to switch to graphics mode");
        return;
    }
    
    serial_puts("Starfield effect. Press 'q' to quit.\n");
    
    // Clear to black
    for (uint16_t y = 0; y < 200; y++) {
        for (uint16_t x = 0; x < 320; x++) {
            vga_plot_pixel(x, y, 0);
        }
    }
    
    // Star data
    typedef struct {
        int x, y;
        int z;  // Depth
    } star_t;
    
    star_t stars[50];
    
    // Initialize stars
    for (int i = 0; i < 50; i++) {
        stars[i].x = (simple_rand() % 320);
        stars[i].y = (simple_rand() % 200);
        stars[i].z = simple_rand() % 256;
    }
    
    // Clear keyboard
    for (int i = 0; i < 10; i++) {
        keyboard_get_scancode();
    }
    
    int running = 1;
    int frame = 0;
    
    while (running) {
        // Draw and update stars
        for (int i = 0; i < 50; i++) {
            // Clear old star
            if (stars[i].x >= 0 && stars[i].x < 320 && 
                stars[i].y >= 0 && stars[i].y < 200) {
                vga_plot_pixel(stars[i].x, stars[i].y, 0);
            }
            
            // Move star closer
            stars[i].z -= 5;
            
            if (stars[i].z <= 0) {
                // Reset star
                stars[i].x = 160 + (simple_rand() % 40 - 20);
                stars[i].y = 100 + (simple_rand() % 40 - 20);
                stars[i].z = 256;
            }
            
            // Project to screen
            int screen_x = 160 + (stars[i].x - 160) * 256 / (stars[i].z + 1);
            int screen_y = 100 + (stars[i].y - 100) * 256 / (stars[i].z + 1);
            
            // Draw star if on screen
            if (screen_x >= 0 && screen_x < 320 && 
                screen_y >= 0 && screen_y < 200) {
                int brightness = 15 * stars[i].z / 256;
                if (brightness < 2) brightness = 2;
                vga_plot_pixel(screen_x, screen_y, brightness);
            }
        }
        
        // Check for 'q' key
        uint8_t scan = keyboard_get_scancode();
        if (scan == 0x10) {
            running = 0;
        }
        
        // Delay
        for (volatile int i = 0; i < 30000; i++) { }
        frame++;
    }
    
    // Return to text mode
    if (vga_set_mode(0x03)) {
        vga_init();
        vga_clear();
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("Starfield demo ended!");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

// Plasma effect
static void cmd_plasma(const char* args) {
    (void)args;
    
    kprint("=== Plasma Effect ===");
    kprint("Switching to 320x200 mode...");
    
    if (!vga_set_mode(VGA_MODE_320x200x256)) {
        kprint("Error: Failed to switch to graphics mode");
        return;
    }
    
    serial_puts("Plasma animation. Press 'q' to quit.\n");
    
    // Clear keyboard
    for (int i = 0; i < 10; i++) {
        keyboard_get_scancode();
    }
    
    int running = 1;
    int frame = 0;
    
    while (running) {
        // Draw plasma using sine waves
        for (uint16_t y = 0; y < 200; y++) {
            for (uint16_t x = 0; x < 320; x++) {
                // Create plasma pattern using mathematical functions
                int value = 0;
                
                // Multiple sine waves at different frequencies
                value += (128 + 127 * 1) / 2;  // Bias
                value += (64 * (x + frame) / 320);
                value += (64 * (y + frame) / 200);
                value += (32 * (x - y + frame) / 320);
                
                // Wrap and quantize to 256 colors
                int color = (value / 4) % 256;
                if (color < 0) color = 0;
                if (color > 255) color = 255;
                
                vga_plot_pixel(x, y, color);
            }
        }
        
        // Check for 'q' key
        uint8_t scan = keyboard_get_scancode();
        if (scan == 0x10) {
            running = 0;
        }
        
        // Delay
        for (volatile int i = 0; i < 50000; i++) { }
        frame += 2;
    }
    
    // Return to text mode
    if (vga_set_mode(0x03)) {
        vga_init();
        vga_clear();
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprint("Plasma demo ended!");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
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
    
    command_register_with_category(
        "bouncy",
        "",
        "Bouncing ball animation",
        "Graphics",
        cmd_bouncy
    );
    
    command_register_with_category(
        "starfield",
        "",
        "3D starfield screensaver effect",
        "Graphics",
        cmd_starfield
    );
    
    command_register_with_category(
        "plasma",
        "",
        "Animated plasma effect",
        "Graphics",
        cmd_plasma
    );
}
