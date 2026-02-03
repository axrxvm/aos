/*
 * === AOS HEADER BEGIN ===
 * ./include/vga.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef VGA_H
#define VGA_H

#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

// VGA Color Palette Constants
#define VGA_COLOR_BLACK         0x0
#define VGA_COLOR_BLUE          0x1
#define VGA_COLOR_GREEN         0x2
#define VGA_COLOR_CYAN          0x3
#define VGA_COLOR_RED           0x4
#define VGA_COLOR_MAGENTA       0x5
#define VGA_COLOR_BROWN         0x6
#define VGA_COLOR_LIGHT_GREY    0x7
#define VGA_COLOR_DARK_GREY     0x8
#define VGA_COLOR_LIGHT_BLUE    0x9
#define VGA_COLOR_LIGHT_GREEN   0xA
#define VGA_COLOR_LIGHT_CYAN    0xB
#define VGA_COLOR_LIGHT_RED     0xC
#define VGA_COLOR_LIGHT_MAGENTA 0xD
#define VGA_COLOR_YELLOW        0xE
#define VGA_COLOR_WHITE         0xF

// VGA Attribute Macro: ((background_color << 4) | foreground_color)
#define VGA_ATTR(fg, bg) (((bg) << 4) | (fg))

// Box drawing characters (using extended ASCII)
#define BOX_SINGLE_TL   '\xDA'  // ┌
#define BOX_SINGLE_TR   '\xBF'  // ┐
#define BOX_SINGLE_BL   '\xC0'  // └
#define BOX_SINGLE_BR   '\xD9'  // ┘
#define BOX_SINGLE_H    '\xC4'  // ─
#define BOX_SINGLE_V    '\xB3'  // │
#define BOX_SINGLE_CX   '\xC5'  // ┼

#define BOX_DOUBLE_TL   '\xC9'  // ╔
#define BOX_DOUBLE_TR   '\xBB'  // ╗
#define BOX_DOUBLE_BL   '\xC8'  // ╚
#define BOX_DOUBLE_BR   '\xBC'  // ╝
#define BOX_DOUBLE_H    '\xCD'  // ═
#define BOX_DOUBLE_V    '\xBA'  // ║

// Cursor styles
typedef enum {
    CURSOR_BLOCK = 0,
    CURSOR_UNDERLINE = 1,
    CURSOR_BLINK = 2
} vga_cursor_style_t;

// Text alignment options
typedef enum {
    TEXT_LEFT = 0,
    TEXT_CENTER = 1,
    TEXT_RIGHT = 2
} vga_text_align_t;

// Basic I/O Functions
void vga_init(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_clear(void);
void vga_clear_all(void); // Clear screen AND scrollback buffer completely
void update_cursor(uint8_t row, uint8_t col);
void vga_erase_char(uint8_t row, uint8_t col); // Note: This function's utility might be limited.
void vga_set_color(uint8_t color_attribute);
uint8_t vga_get_row(void);
uint8_t vga_get_col(void);
void vga_backspace(void); // Properly erase character at cursor and move back
void vga_set_position(uint8_t row, uint8_t col); // Set cursor position manually
void vga_scroll_up(void); // Scroll the screen content up by one line (saves to scrollback)
void vga_scroll_down(void); // Scroll the view down (toward newer content)
void vga_scroll_up_view(void); // Scroll the view up (toward older content in scrollback)
void vga_scroll_to_bottom(void); // Return to live view (scroll offset = 0)
uint16_t* vga_get_buffer(void); // Get the VGA buffer pointer

// Advanced Drawing Functions
void vga_draw_char(uint8_t row, uint8_t col, char c); // Draw character at specific position
void vga_draw_char_color(uint8_t row, uint8_t col, char c, uint8_t color); // Draw with custom color
void vga_fill_rect(uint8_t row, uint8_t col, uint8_t width, uint8_t height, char c); // Fill rectangle
void vga_fill_rect_color(uint8_t row, uint8_t col, uint8_t width, uint8_t height, char c, uint8_t color);
void vga_draw_box(uint8_t row, uint8_t col, uint8_t width, uint8_t height, int double_line); // Draw box border
void vga_draw_box_color(uint8_t row, uint8_t col, uint8_t width, uint8_t height, int double_line, uint8_t color);
void vga_draw_hline(uint8_t row, uint8_t col, uint8_t width, char c); // Draw horizontal line
void vga_draw_vline(uint8_t col, uint8_t row, uint8_t height, char c); // Draw vertical line
void vga_draw_hline_color(uint8_t row, uint8_t col, uint8_t width, char c, uint8_t color);
void vga_draw_vline_color(uint8_t col, uint8_t row, uint8_t height, char c, uint8_t color);

// Text Formatting Functions
void vga_puts_at(uint8_t row, uint8_t col, const char *s); // Print string at position
void vga_puts_at_color(uint8_t row, uint8_t col, const char *s, uint8_t color);
void vga_puts_aligned(uint8_t row, vga_text_align_t align, const char *s); // Print aligned text
void vga_puts_aligned_color(uint8_t row, vga_text_align_t align, const char *s, uint8_t color);
void vga_printf_at(uint8_t row, uint8_t col, const char *fmt, ...); // Printf at position
void vga_printf_color(const char *fmt, uint8_t color, ...); // Printf with color

// Cursor Style Functions
void vga_set_cursor_style(vga_cursor_style_t style); // Set cursor appearance
void vga_enable_cursor(void);
void vga_disable_cursor(void);
void vga_hide_cursor(void); // Hide cursor without disabling it

// Color/Palette Functions
void vga_invert_colors(void); // Swap fg and bg colors
void vga_brighten_color(uint8_t color); // Make color brighter
uint8_t vga_blend_colors(uint8_t fg, uint8_t bg, uint8_t alpha); // Simple color blending
void vga_color_gradient(uint8_t start_color, uint8_t end_color, uint8_t steps); // Display gradient

// Screen Effects Functions
void vga_clear_line(uint8_t row); // Clear specific line
void vga_clear_region(uint8_t row, uint8_t col, uint8_t width, uint8_t height);
void vga_fill_screen(char c, uint8_t color); // Fill entire screen with char
void vga_screen_wipe(uint8_t direction, uint8_t speed); // Wipe effect (0=up, 1=down, 2=left, 3=right)
void vga_screen_fade_to_color(uint8_t color, uint8_t steps); // Fade effect

// Utility Functions
void vga_frame_buffer(void); // Double-buffer the VGA output
void vga_refresh_display(void); // Manually refresh display
void vga_get_text_at(uint8_t row, uint8_t col, char *buf, uint8_t len); // Read text from screen
void vga_get_color_at(uint8_t row, uint8_t col); // Get color at position
uint16_t vga_measure_text(const char *s); // Get text width
void vga_draw_progress_bar(uint8_t row, uint8_t col, uint8_t width, uint8_t percent, uint8_t color);

#endif
