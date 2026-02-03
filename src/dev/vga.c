/*
 * === AOS HEADER BEGIN ===
 * ./src/dev/vga.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <io.h>
#include <vga.h>
#include <stdlib.h>
#include <string.h>

#define SCROLLBACK_LINES 100

static uint16_t* vga_buffer = (uint16_t*)VGA_ADDRESS;
static uint8_t vga_row = 0, vga_col = 0;
static uint8_t vga_color = 0x0F; // Default: light gray on black
static int vga_cursor_visible = 1;
static vga_cursor_style_t vga_cursor_style = CURSOR_BLOCK;

// Scrollback buffer: stores lines that have scrolled off the top
static uint16_t scrollback_buffer[SCROLLBACK_LINES][VGA_WIDTH];
static uint32_t scrollback_count = 0; // Number of lines in scrollback
static uint32_t scrollback_start = 0; // Index of oldest line (circular buffer)
static int32_t scroll_offset = 0; // How many lines we've scrolled back (0 = at bottom)

// Current buffer: stores the actual live content (not affected by scrolling view)
static uint16_t current_buffer[VGA_HEIGHT][VGA_WIDTH];

// Double-buffering
static uint16_t frame_buffer[VGA_HEIGHT * VGA_WIDTH];
static int use_frame_buffer = 0;

// Forward declarations
static void vga_render_with_offset(void);
static int vga_strlen(const char *s);
static void vga_itoa(int value, char *str, int radix);

void vga_init(void) {
    vga_clear();
    vga_row = 0;
    vga_col = 0;
    scrollback_count = 0;
    scrollback_start = 0;
    scroll_offset = 0;
    
    // Initialize cursor with blinking style
    vga_set_cursor_style(CURSOR_BLINK);
    vga_enable_cursor();
    update_cursor(vga_row, vga_col);
}

void vga_putc(char c) {
    // If we're scrolled back, auto-scroll to bottom on new output
    if (scroll_offset > 0) {
        vga_scroll_to_bottom();
    }
    
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\b') {
        // Handle backspace: move cursor back and erase character
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            // Wrap to previous line
            vga_row--;
            vga_col = VGA_WIDTH - 1;
        }
        // Erase the character at the current position
        uint16_t entry = (uint16_t)(vga_color << 8 | ' ');
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = entry;
        current_buffer[vga_row][vga_col] = entry;
    } else {
        uint16_t entry = (uint16_t)(vga_color << 8 | c);
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = entry;
        current_buffer[vga_row][vga_col] = entry;
        vga_col++;
    }
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll_up();
        vga_row = VGA_HEIGHT - 1;
    }
    update_cursor(vga_row, vga_col);
}

void vga_puts(const char *s) {
    while (*s) {
        vga_putc(*s++);
    }
}

void vga_clear(void) {
    // Save all current screen lines to scrollback before clearing
    for (int row = 0; row < VGA_HEIGHT; row++) {
        // Check if line has any non-space content
        int has_content = 0;
        for (int col = 0; col < VGA_WIDTH; col++) {
            uint16_t entry = current_buffer[row][col];
            char c = entry & 0xFF;
            if (c != ' ') {
                has_content = 1;
                break;
            }
        }
        
        // Only save lines with content
        if (has_content || row < vga_row) {
            uint32_t scrollback_index = (scrollback_start + scrollback_count) % SCROLLBACK_LINES;
            for (int col = 0; col < VGA_WIDTH; col++) {
                scrollback_buffer[scrollback_index][col] = current_buffer[row][col];
            }
            
            // Update scrollback tracking
            if (scrollback_count < SCROLLBACK_LINES) {
                scrollback_count++;
            } else {
                // Buffer is full, wrap around
                scrollback_start = (scrollback_start + 1) % SCROLLBACK_LINES;
            }
        }
    }
    
    // Now clear the screen
    uint16_t entry = (uint16_t)(vga_color << 8 | ' ');
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga_buffer[row * VGA_WIDTH + col] = entry;
            current_buffer[row][col] = entry;
        }
    }
    vga_row = 0;
    vga_col = 0;
    update_cursor(vga_row, vga_col);
}

void vga_clear_all(void) {
    // Clear both screen AND scrollback buffer completely
    uint16_t entry = (uint16_t)(vga_color << 8 | ' ');
    
    // Clear visible screen
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga_buffer[row * VGA_WIDTH + col] = entry;
            current_buffer[row][col] = entry;
        }
    }
    
    // Clear scrollback buffer
    for (int i = 0; i < SCROLLBACK_LINES; i++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            scrollback_buffer[i][col] = entry;
        }
    }
    
    // Reset all state
    scrollback_count = 0;
    scrollback_start = 0;
    scroll_offset = 0;
    vga_row = 0;
    vga_col = 0;
    update_cursor(vga_row, vga_col);
}

void update_cursor(uint8_t row, uint8_t col) {
    uint16_t pos = row * VGA_WIDTH + col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_erase_char(uint8_t row, uint8_t col) {
    if (col > 5) { // Ensure we don't erase the "aOS> " prompt
        vga_buffer[row * VGA_WIDTH + col - 1] = (uint16_t)(vga_color << 8 | ' '); // Erase with current color
    }
}

void vga_set_color(uint8_t color_attribute) {
    vga_color = color_attribute; // Update the global color attribute
    // Optionally, update the entire screen with the new color if needed
    // This is not done here for performance, but can be added if desired
}

uint8_t vga_get_row(void) {
    return vga_row;
}

uint8_t vga_get_col(void) {
    return vga_col;
}

void vga_backspace(void) {
    // Move cursor back
    if (vga_col > 0) {
        vga_col--;
    } else if (vga_row > 0) {
        // Wrap to previous line
        vga_row--;
        vga_col = VGA_WIDTH - 1;
    }
    // Erase the character at the current position
    uint16_t entry = (uint16_t)(vga_color << 8 | ' ');
    vga_buffer[vga_row * VGA_WIDTH + vga_col] = entry;
    current_buffer[vga_row][vga_col] = entry;
    // Update cursor position
    update_cursor(vga_row, vga_col);
}

void vga_set_position(uint8_t row, uint8_t col) {
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        vga_row = row;
        vga_col = col;
        update_cursor(vga_row, vga_col);
    }
}

void vga_scroll_up(void) {
    // Save the top line to scrollback buffer before scrolling
    uint32_t scrollback_index = (scrollback_start + scrollback_count) % SCROLLBACK_LINES;
    for (int col = 0; col < VGA_WIDTH; col++) {
        scrollback_buffer[scrollback_index][col] = current_buffer[0][col];
    }
    
    // Update scrollback tracking
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    } else {
        // Buffer is full, wrap around (oldest line gets overwritten)
        scrollback_start = (scrollback_start + 1) % SCROLLBACK_LINES;
    }
    
    // Move all lines up by one in both buffers
    for (int row = 0; row < VGA_HEIGHT - 1; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            uint16_t entry = current_buffer[row + 1][col];
            vga_buffer[row * VGA_WIDTH + col] = entry;
            current_buffer[row][col] = entry;
        }
    }
    // Clear the bottom line
    uint16_t entry = (uint16_t)(vga_color << 8 | ' ');
    for (int col = 0; col < VGA_WIDTH; col++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = entry;
        current_buffer[VGA_HEIGHT - 1][col] = entry;
    }
    // Keep cursor at bottom
    if (vga_row > 0) {
        vga_row--;
    }
    update_cursor(vga_row, vga_col);
}

void vga_scroll_down(void) {
    // Only scroll down if we have scrollback and are currently scrolled back
    if (scrollback_count == 0 || scroll_offset <= 0) {
        return; // Nothing to scroll to
    }
    
    scroll_offset--;
    vga_render_with_offset();
}

void vga_scroll_up_view(void) {
    // Scroll the view up (show older content from scrollback)
    if (scrollback_count == 0) {
        return; // No scrollback available
    }
    
    // Can't scroll back more than we have in the buffer
    if (scroll_offset < (int32_t)scrollback_count) {
        scroll_offset++;
        vga_render_with_offset();
    }
}

void vga_scroll_to_bottom(void) {
    if (scroll_offset == 0) {
        return; // Already at bottom
    }
    scroll_offset = 0;
    vga_render_with_offset();
}

static void vga_render_with_offset(void) {
    // Render the screen based on current scroll offset
    if (scroll_offset == 0) {
        // At the bottom - restore current buffer to VGA buffer
        for (int row = 0; row < VGA_HEIGHT; row++) {
            for (int col = 0; col < VGA_WIDTH; col++) {
                vga_buffer[row * VGA_WIDTH + col] = current_buffer[row][col];
            }
        }
        update_cursor(vga_row, vga_col);
        return;
    }
    
    // Calculate how many lines to take from scrollback
    int32_t lines_from_scrollback = (scroll_offset > VGA_HEIGHT) ? VGA_HEIGHT : scroll_offset;
    int32_t lines_from_current = VGA_HEIGHT - lines_from_scrollback;
    
    // Fill from scrollback (oldest visible lines first)
    for (int row = 0; row < lines_from_scrollback; row++) {
        // Calculate which scrollback line to show
        int32_t scrollback_line = scrollback_count - scroll_offset + row;
        if (scrollback_line >= 0) {
            uint32_t scrollback_index = (scrollback_start + scrollback_line) % SCROLLBACK_LINES;
            for (int col = 0; col < VGA_WIDTH; col++) {
                vga_buffer[row * VGA_WIDTH + col] = scrollback_buffer[scrollback_index][col];
            }
        } else {
            // Empty line
            for (int col = 0; col < VGA_WIDTH; col++) {
                vga_buffer[row * VGA_WIDTH + col] = (uint16_t)(vga_color << 8 | ' ');
            }
        }
    }
    
    // Fill from current buffer (not vga_buffer, since that's been overwritten)
    for (int row = 0; row < lines_from_current; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga_buffer[(lines_from_scrollback + row) * VGA_WIDTH + col] = current_buffer[row][col];
        }
    }
    
    // Hide cursor when scrolled back
    update_cursor(VGA_HEIGHT, 0);
}

uint16_t* vga_get_buffer(void) {
    return vga_buffer;
}

void vga_draw_char(uint8_t row, uint8_t col, char c) {
    vga_draw_char_color(row, col, c, vga_color);
}

void vga_draw_char_color(uint8_t row, uint8_t col, char c, uint8_t color) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    
    uint16_t entry = (uint16_t)(color << 8 | c);
    vga_buffer[row * VGA_WIDTH + col] = entry;
    current_buffer[row][col] = entry;
}

void vga_fill_rect(uint8_t row, uint8_t col, uint8_t width, uint8_t height, char c) {
    vga_fill_rect_color(row, col, width, height, c, vga_color);
}

void vga_fill_rect_color(uint8_t row, uint8_t col, uint8_t width, uint8_t height, char ch, uint8_t color) {
    for (uint8_t r = row; r < row + height && r < VGA_HEIGHT; r++) {
        for (uint8_t c = col; c < col + width && c < VGA_WIDTH; c++) {
            vga_draw_char_color(r, c, ch, color);
        }
    }
}

void vga_draw_box(uint8_t row, uint8_t col, uint8_t width, uint8_t height, int double_line) {
    vga_draw_box_color(row, col, width, height, double_line, vga_color);
}

void vga_draw_box_color(uint8_t row, uint8_t col, uint8_t width, uint8_t height, int double_line, uint8_t color) {
    if (width < 2 || height < 2 || row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    
    char tl, tr, bl, br, h, v;
    
    if (double_line) {
        tl = BOX_DOUBLE_TL;
        tr = BOX_DOUBLE_TR;
        bl = BOX_DOUBLE_BL;
        br = BOX_DOUBLE_BR;
        h = BOX_DOUBLE_H;
        v = BOX_DOUBLE_V;
    } else {
        tl = BOX_SINGLE_TL;
        tr = BOX_SINGLE_TR;
        bl = BOX_SINGLE_BL;
        br = BOX_SINGLE_BR;
        h = BOX_SINGLE_H;
        v = BOX_SINGLE_V;
    }
    
    // Top-left corner
    vga_draw_char_color(row, col, tl, color);
    
    // Top-right corner
    vga_draw_char_color(row, col + width - 1, tr, color);
    
    // Bottom-left corner
    vga_draw_char_color(row + height - 1, col, bl, color);
    
    // Bottom-right corner
    vga_draw_char_color(row + height - 1, col + width - 1, br, color);
    
    // Top and bottom lines
    for (uint8_t c = col + 1; c < col + width - 1 && c < VGA_WIDTH; c++) {
        vga_draw_char_color(row, c, h, color);
        vga_draw_char_color(row + height - 1, c, h, color);
    }
    
    // Left and right lines
    for (uint8_t r = row + 1; r < row + height - 1 && r < VGA_HEIGHT; r++) {
        vga_draw_char_color(r, col, v, color);
        vga_draw_char_color(r, col + width - 1, v, color);
    }
}

void vga_draw_hline(uint8_t row, uint8_t col, uint8_t width, char c) {
    vga_draw_hline_color(row, col, width, c, vga_color);
}

void vga_draw_hline_color(uint8_t row, uint8_t col, uint8_t width, char c, uint8_t color) {
    for (uint8_t c_idx = col; c_idx < col + width && c_idx < VGA_WIDTH; c_idx++) {
        vga_draw_char_color(row, c_idx, c, color);
    }
}

void vga_draw_vline(uint8_t col, uint8_t row, uint8_t height, char c) {
    vga_draw_vline_color(col, row, height, c, vga_color);
}

void vga_draw_vline_color(uint8_t col, uint8_t row, uint8_t height, char c, uint8_t color) {
    for (uint8_t r = row; r < row + height && r < VGA_HEIGHT; r++) {
        vga_draw_char_color(r, col, c, color);
    }
}


// TEXT FORMATTING FUNCTIONS

static int vga_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

void vga_puts_at(uint8_t row, uint8_t col, const char *s) {
    vga_puts_at_color(row, col, s, vga_color);
}

void vga_puts_at_color(uint8_t row, uint8_t col, const char *s, uint8_t color) {
    if (row >= VGA_HEIGHT) return;
    
    uint8_t c = col;
    while (*s && c < VGA_WIDTH) {
        vga_draw_char_color(row, c, *s, color);
        s++;
        c++;
    }
}

void vga_puts_aligned(uint8_t row, vga_text_align_t align, const char *s) {
    vga_puts_aligned_color(row, align, s, vga_color);
}

void vga_puts_aligned_color(uint8_t row, vga_text_align_t align, const char *s, uint8_t color) {
    if (row >= VGA_HEIGHT) return;
    
    int len = vga_strlen(s);
    uint8_t col = 0;
    
    switch (align) {
        case TEXT_LEFT:
            col = 0;
            break;
        case TEXT_CENTER:
            col = (VGA_WIDTH - len) / 2;
            break;
        case TEXT_RIGHT:
            col = VGA_WIDTH - len;
            break;
    }
    
    vga_puts_at_color(row, col, s, color);
}

void vga_printf_at(uint8_t row, uint8_t col, const char *fmt, ...) {
    // Simple implementation - just print to current position for now
    (void)row; (void)col;
    vga_puts(fmt);
}

void vga_printf_color(const char *fmt, uint8_t color, ...) {
    // Simple implementation - just print with color
    uint8_t saved_color = vga_color;
    vga_set_color(color);
    vga_puts(fmt);
    vga_set_color(saved_color);
}

// CURSOR STYLE FUNCTIONS
void vga_set_cursor_style(vga_cursor_style_t style) {
    vga_cursor_style = style;
    
    // Read current cursor start line to preserve other bits
    outb(0x3D4, 0x0A);
    uint8_t cursor_start = inb(0x3D5);
    
    // Clear bits 0-4 and 5 (start line and blink bit)
    cursor_start &= 0xC0;  // Keep only bits 6 and 7
    
    // Always enable cursor by clearing bit 6
    cursor_start &= ~0x40;
    
    switch (style) {
        case CURSOR_UNDERLINE:
            cursor_start |= 0x0E;  // Set cursor start to line 14
            break;
        case CURSOR_BLOCK:
            cursor_start |= 0x00;  // Set cursor start to line 0
            break;
        case CURSOR_BLINK:
            cursor_start |= 0x0E;  // Set cursor start to line 14
            cursor_start |= 0x20;  // Set blink bit (bit 5)
            break;
    }
    
    // Write cursor start line
    outb(0x3D4, 0x0A);
    outb(0x3D5, cursor_start);
    
    // Set cursor end line to 15 (full height)
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);
}

void vga_enable_cursor(void) {
    vga_cursor_visible = 1;
    
    // Forcefully enable cursor at hardware level
    // Cursor Start Register (0x0A): bits 0-4 = start scanline, bit 5 = blink, bit 6 = disable cursor
    // Set cursor start to line 14 with blinking enabled and cursor enabled (bit 6 = 0)
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x2E);  // 0x2E = 0010 1110 (blink=1, disable=0, start=14)
    
    // Cursor End Register (0x0B): bits 0-4 = end scanline
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x0F);  // End at scanline 15
    
    // Update cursor position
    update_cursor(vga_row, vga_col);
}

void vga_disable_cursor(void) {
    vga_cursor_visible = 0;
    // Move cursor off screen
    update_cursor(VGA_HEIGHT, 0);
}

void vga_hide_cursor(void) {
    vga_disable_cursor();
}


// COLOR/PALETTE FUNCTIONS


void vga_invert_colors(void) {
    uint8_t fg = vga_color & 0x0F;
    uint8_t bg = (vga_color >> 4) & 0x0F;
    vga_set_color(VGA_ATTR(bg, fg));
}

void vga_brighten_color(uint8_t color) {
    uint8_t fg = color & 0x0F;
    uint8_t bg = (color >> 4) & 0x0F;
    
    // Brighten foreground by adding 0x08 if not already bright
    if (fg < 8) fg += 8;
    
    vga_set_color(VGA_ATTR(fg, bg));
}

uint8_t vga_blend_colors(uint8_t fg, uint8_t bg, uint8_t alpha) {
    // Simple blend: if alpha >= 128, use fg, else use bg
    if (alpha >= 128) {
        return VGA_ATTR(fg, bg);
    } else {
        return VGA_ATTR(bg, fg);
    }
}

void vga_color_gradient(uint8_t start_color, uint8_t end_color, uint8_t steps) {
    // Display a gradient from start to end color
    uint8_t start_fg = start_color & 0x0F;
    uint8_t end_fg = end_color & 0x0F;
    
    uint16_t col_width = VGA_WIDTH / steps;
    
    for (uint8_t i = 0; i < steps && i < VGA_WIDTH; i++) {
        // Interpolate color
        uint8_t blend = (i * 255) / steps;
        uint8_t color = (blend >= 128) ? end_fg : start_fg;
        vga_draw_hline_color(VGA_HEIGHT - 1, i * col_width, col_width, '=', VGA_ATTR(color, 0));
    }
}


// SCREEN EFFECTS FUNCTIONS


void vga_clear_line(uint8_t row) {
    if (row >= VGA_HEIGHT) return;
    
    uint16_t entry = (uint16_t)(vga_color << 8 | ' ');
    for (uint8_t col = 0; col < VGA_WIDTH; col++) {
        vga_buffer[row * VGA_WIDTH + col] = entry;
        current_buffer[row][col] = entry;
    }
}

void vga_clear_region(uint8_t row, uint8_t col, uint8_t width, uint8_t height) {
    for (uint8_t r = row; r < row + height && r < VGA_HEIGHT; r++) {
        for (uint8_t c = col; c < col + width && c < VGA_WIDTH; c++) {
            vga_draw_char_color(r, c, ' ', vga_color);
        }
    }
}

void vga_fill_screen(char c, uint8_t color) {
    uint16_t entry = (uint16_t)(color << 8 | c);
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga_buffer[row * VGA_WIDTH + col] = entry;
            current_buffer[row][col] = entry;
        }
    }
}

void vga_screen_wipe(uint8_t direction, uint8_t speed) {
    // Wipe screen in specified direction: 0=up, 1=down, 2=left, 3=right
    uint16_t blank = (uint16_t)(vga_color << 8 | ' ');
    
    switch (direction) {
        case 0: // Wipe up
            for (uint8_t row = 0; row < VGA_HEIGHT; row++) {
                for (uint8_t col = 0; col < VGA_WIDTH; col++) {
                    vga_buffer[row * VGA_WIDTH + col] = blank;
                    current_buffer[row][col] = blank;
                }
            }
            break;
        case 1: // Wipe down
            for (int row = VGA_HEIGHT - 1; row >= 0; row--) {
                for (uint8_t col = 0; col < VGA_WIDTH; col++) {
                    vga_buffer[row * VGA_WIDTH + col] = blank;
                    current_buffer[row][col] = blank;
                }
            }
            break;
        case 2: // Wipe left
            for (uint8_t row = 0; row < VGA_HEIGHT; row++) {
                for (uint8_t col = 0; col < VGA_WIDTH; col++) {
                    vga_buffer[row * VGA_WIDTH + col] = blank;
                    current_buffer[row][col] = blank;
                }
            }
            break;
        case 3: // Wipe right
            for (uint8_t row = 0; row < VGA_HEIGHT; row++) {
                for (int col = VGA_WIDTH - 1; col >= 0; col--) {
                    vga_buffer[row * VGA_WIDTH + col] = blank;
                    current_buffer[row][col] = blank;
                }
            }
            break;
    }
    
    (void)speed; // Unused for now, could add delay loop
}

void vga_screen_fade_to_color(uint8_t color, uint8_t steps) {
    // Fade screen to solid color
    for (uint8_t step = 0; step < steps; step++) {
        uint16_t entry = (uint16_t)(color << 8 | ' ');
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            vga_buffer[i] = entry;
        }
    }
}


// UTILITY FUNCTIONS


void vga_frame_buffer(void) {
    use_frame_buffer = 1;
}

void vga_refresh_display(void) {
    if (use_frame_buffer) {
        // Copy frame buffer to VGA buffer
        for (int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            vga_buffer[i] = frame_buffer[i];
        }
    }
}

void vga_get_text_at(uint8_t row, uint8_t col, char *buf, uint8_t len) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    
    uint8_t c = col;
    uint8_t idx = 0;
    
    while (idx < len - 1 && c < VGA_WIDTH) {
        uint16_t entry = current_buffer[row][c];
        char ch = entry & 0xFF;
        buf[idx++] = ch;
        c++;
    }
    buf[idx] = '\0';
}

void vga_get_color_at(uint8_t row, uint8_t col) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    
    uint16_t entry = current_buffer[row][col];
    uint8_t color = (entry >> 8) & 0xFF;
    vga_set_color(color);
}

uint16_t vga_measure_text(const char *s) {
    uint16_t len = 0;
    while (s[len]) len++;
    return len;
}

void vga_draw_progress_bar(uint8_t row, uint8_t col, uint8_t width, uint8_t percent, uint8_t color) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    if (width < 2) return;
    
    // Clamp percent to 0-100
    if (percent > 100) percent = 100;
    
    // Calculate filled width
    uint8_t filled = (width - 2) * percent / 100;
    
    // Draw border
    vga_draw_char_color(row, col, '[', color);
    vga_draw_char_color(row, col + width - 1, ']', color);
    
    // Draw filled portion
    for (uint8_t i = 0; i < filled && col + 1 + i < VGA_WIDTH; i++) {
        vga_draw_char_color(row, col + 1 + i, '=', color);
    }
    
    // Draw empty portion
    for (uint8_t i = filled; i < width - 2 && col + 1 + i < VGA_WIDTH; i++) {
        vga_draw_char_color(row, col + 1 + i, ' ', color);
    }
}

// Simple integer to string conversion (minimal implementation)
static void vga_itoa(int value, char *str, int radix) __attribute__((unused));
static void vga_itoa(int value, char *str, int radix) {
    (void)value; (void)str; (void)radix;
    // Placeholder for now
}
