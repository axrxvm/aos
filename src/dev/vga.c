/*
 * === AOS HEADER BEGIN ===
 * src/dev/vga.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <io.h>
#include <vga.h>
#include <stdlib.h>
#include <string.h>
#include <serial.h>
#include <arch/paging.h>
#include <multiboot.h>

#define SCROLLBACK_LINES 100

// GLOBAL STATE VARIABLES
// Text Mode State
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

// Graphics Mode State
static vga_mode_info_t current_mode_info;
static uint8_t* graphics_framebuffer = NULL;
static uint8_t* back_buffer = NULL;
static int graphics_mode_enabled = 0;
static int double_buffer_enabled = 0;

// VGA font backup - plane 2 holds character bitmaps (256 chars * 32 bytes each)
#define VGA_FONT_SIZE (256 * 32)
static uint8_t saved_font[VGA_FONT_SIZE];
static int font_saved = 0;
static int vbe_available = 0;

// VBE Information
static vbe_info_block_t vbe_info;
static vbe_mode_info_t vbe_mode_info;

// Multiboot-compatible boot information from the active boot path (GRUB or ABL)
static multiboot_info_t* grub_mbi = NULL;


// FORWARD DECLARATIONS


static void vga_render_with_offset(void);
static int vga_strlen(const char *s);
static void vga_itoa(int value, char *str, int radix);
static uint8_t vga_find_closest_color(uint8_t r, uint8_t g, uint8_t b);
static int vga_bios_call(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx);
static inline int abs(int x) { return x < 0 ? -x : x; }
static inline void swap(int* a, int* b) { int t = *a; *a = *b; *b = t; }

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

void vga_set_multiboot_info(multiboot_info_t* mbi) {
    grub_mbi = mbi;
    serial_puts("VGA: Boot info registered (multiboot-compatible)\n");
    
    if (mbi && (mbi->flags & MULTIBOOT_INFO_VBE_INFO)) {
        serial_puts("VGA: Multiboot provides VBE information\n");
    }
    if (mbi && (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) {
        serial_puts("VGA: Multiboot provides framebuffer information\n");
    }
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


// VBE/VESA BIOS EXTENSION FUNCTIONS | CHANGE ONLY IF U HAVE THE MANNUAL OPEN DUMBO

// V8086 Mode Structures for Real Mode BIOS Calls
typedef struct {
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t eflags;
    uint16_t es, ds, fs, gs;
} __attribute__((packed)) v86_regs_t;

// Real mode memory area for BIOS calls (lower 1MB)
#define REAL_MODE_BIOS_DATA 0x0000  // BIOS data area
#define REAL_MODE_IVT       0x0000  // Interrupt vector table
#define REAL_MODE_BUFFER    0x8000  // Scratch buffer at 0x8000

// Execute INT 0x10 BIOS call using v8086 mode
static int vga_bios_int10(v86_regs_t* regs) {
    if (!regs) return -1;
    
    // Save current state
    uintptr_t saved_eflags;
#if defined(ARCH_X86_64)
    __asm__ volatile("pushfq; popq %0" : "=r"(saved_eflags));
#else
    __asm__ volatile("pushf; pop %0" : "=r"(saved_eflags));
#endif
    
    // For now, use a simplified approach: call BIOS directly if in real mode
    // In a full implementation, this would:
    // 1. Set up v8086 mode task
    // 2. Map lower 1MB of memory
    // 3. Set up IVT and BIOS data area
    // 4. Execute INT 0x10 in v8086 context
    // 5. Capture return values
    
    // Check if we can access BIOS memory (0x0000-0xFFFFF)
    volatile uint16_t* ivt = (volatile uint16_t*)0x0000;
    if (ivt == NULL) {
        return -1;  // Cannot access real mode memory
    }
    
    // For VBE calls, we need to use the linear framebuffer approach
    // Most modern systems support VBE 2.0+ with linear framebuffer
    // which doesn't require real mode switching
    
    return 0;  // Success
}

// Improved BIOS call wrapper with proper register handling
static int vga_bios_call(uint16_t ax, uint16_t bx, uint16_t cx, uint16_t dx) {
    v86_regs_t regs;
    memset(&regs, 0, sizeof(regs));
    
    regs.eax = ax;
    regs.ebx = bx;
    regs.ecx = cx;
    regs.edx = dx;
    
    // Set up segment registers for real mode
    regs.es = 0x0000;
    regs.ds = 0x0000;
    regs.fs = 0x0000;
    regs.gs = 0x0000;
    
    int result = vga_bios_int10(&regs);
    
    // Check VBE return code in AX
    if (result == 0 && (regs.eax & 0xFFFF) == 0x004F) {
        return 1;  // VBE call successful
    }
    
    return 0;  // Failed or not supported
}

int vga_detect_vbe(void) {
    // Use bootloader-provided multiboot-compatible VBE information.
    if (grub_mbi && (grub_mbi->flags & MULTIBOOT_INFO_VBE_INFO)) {
        // The bootloader has already queried VBE info from BIOS in real mode.
        multiboot_vbe_controller_info_t* ctrl_info = 
            (multiboot_vbe_controller_info_t*)(uint32_t)grub_mbi->vbe_control_info;
        
        if (ctrl_info) {
            // Copy VBE controller info
            for (int i = 0; i < 4; i++) {
                vbe_info.signature[i] = ctrl_info->signature[i];
            }
            vbe_info.version = ctrl_info->version;
            vbe_info.total_memory = ctrl_info->total_memory;
            
            char ver_str[32];
            serial_puts("VBE detected from boot info: version ");
            // Version is in BCD format (e.g., 0x0300 = 3.0)
            uint8_t major = (vbe_info.version >> 8) & 0xFF;
            uint8_t minor = vbe_info.version & 0xFF;
            // Simple itoa for version
            ver_str[0] = '0' + major;
            ver_str[1] = '.';
            ver_str[2] = '0' + (minor >> 4);
            ver_str[3] = '\n';
            ver_str[4] = '\0';
            serial_puts(ver_str);
            
            serial_puts("VBE Video Memory: ");
            // total_memory is in 64KB blocks
            vbe_available = 1;
            return 1;
        }
    }
    
    // Check if framebuffer info is available (alternative to VBE)
    if (grub_mbi && (grub_mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) {
        serial_puts("Framebuffer info available from boot info (no VBE struct)\n");
        serial_puts("Using direct framebuffer access\n");
        vbe_available = 1;
        vbe_info.version = 0x0300;  // Assume VBE 3.0 compatible
        return 1;
    }
    
    // Fallback: no multiboot VBE info available
    serial_puts("No VBE info from bootloader, using legacy VGA only\n");
    vbe_available = 0;
    return 0;
}

int vga_get_vbe_info(vbe_info_block_t* info) {
    if (!vbe_available) return 0;
    if (!info) return 0;
    
    // Copy cached VBE info
    for (int i = 0; i < 4; i++) {
        info->signature[i] = vbe_info.signature[i];
    }
    info->version = 0x0300;  // VBE 3.0
    info->total_memory = 16;  // 16 * 64KB = 1MB
    
    return 1;
}

int vga_get_vbe_mode_info(uint16_t mode, vbe_mode_info_t* info) {
    if (!vbe_available || !info) return 0;
    
    // First, try to get mode info from bootloader-provided multiboot-compatible data.
    if (grub_mbi && (grub_mbi->flags & MULTIBOOT_INFO_VBE_INFO)) {
        multiboot_vbe_mode_info_t* grub_mode_info = 
            (multiboot_vbe_mode_info_t*)(uint32_t)grub_mbi->vbe_mode_info;
        
        // Check if the requested mode matches the current mode set by GRUB
        if (grub_mode_info && grub_mbi->vbe_mode == mode) {
            serial_puts("Using VBE mode info from boot info for current mode\n");
            
            // Copy mode information from GRUB
            info->attributes = grub_mode_info->attributes;
            info->width = grub_mode_info->width;
            info->height = grub_mode_info->height;
            info->bpp = grub_mode_info->bpp;
            info->framebuffer = grub_mode_info->framebuffer;
            info->pitch = grub_mode_info->pitch;
            
            return 1;
        }
    }
    
    // Alternative: Use framebuffer info if available
    if (grub_mbi && (grub_mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) {
        serial_puts("Using framebuffer info from boot info\n");
        
        info->attributes = VBE_MODE_SUPPORTED | VBE_MODE_COLOR | VBE_MODE_GRAPHICS | VBE_MODE_LINEAR_FB;
        info->width = grub_mbi->framebuffer_width;
        info->height = grub_mbi->framebuffer_height;
        info->bpp = grub_mbi->framebuffer_bpp;
        info->framebuffer = (uint32_t)grub_mbi->framebuffer_addr;
        info->pitch = grub_mbi->framebuffer_pitch;
        
        return 1;
    }
    
    // Fallback: Provide standard mode configurations
    serial_puts("VBE mode info BIOS call failed, using defaults\n");
    info->attributes = VBE_MODE_SUPPORTED | VBE_MODE_COLOR | VBE_MODE_GRAPHICS | VBE_MODE_LINEAR_FB;
    
    switch (mode) {
        case VGA_MODE_320x200x256:
            info->width = 320;
            info->height = 200;
            info->bpp = 8;
            info->framebuffer = 0xA0000;
            info->pitch = 320;
            break;
        case VBE_MODE_640x480x256:
            info->width = 640;
            info->height = 480;
            info->bpp = 8;
            info->framebuffer = 0xE0000000;
            info->pitch = 640;
            break;
        case VBE_MODE_800x600x256:
            info->width = 800;
            info->height = 600;
            info->bpp = 8;
            info->framebuffer = 0xE0000000;
            info->pitch = 800;
            break;
        case VBE_MODE_1024x768x256:
            info->width = 1024;
            info->height = 768;
            info->bpp = 8;
            info->framebuffer = 0xE0000000;
            info->pitch = 1024;
            break;
        case VBE_MODE_640x480x16M:
            info->width = 640;
            info->height = 480;
            info->bpp = 24;
            info->framebuffer = 0xE0000000;
            info->pitch = 640 * 3;
            break;
        case VBE_MODE_800x600x16M:
            info->width = 800;
            info->height = 600;
            info->bpp = 24;
            info->framebuffer = 0xE0000000;
            info->pitch = 800 * 3;
            break;
        case VBE_MODE_1024x768x16M:
            info->width = 1024;
            info->height = 768;
            info->bpp = 24;
            info->framebuffer = 0xE0000000;
            info->pitch = 1024 * 3;
            break;
        default:
            return 0;
    }
    
    return 1;
}

// Save VGA font from plane 2 before entering graphics mode
static void vga_save_font(void) {
    serial_puts("Saving VGA font from plane 2...\n");
    
    // Set up to read plane 2 (font data)
    outb(0x3CE, 0x04);  // Read Map Select register
    outb(0x3CF, 0x02);  // Select plane 2
    
    outb(0x3CE, 0x05);  // Graphics Mode register
    outb(0x3CF, 0x00);  // Read mode 0 (direct read)
    
    outb(0x3CE, 0x06);  // Miscellaneous Graphics register
    outb(0x3CF, 0x04);  // Map at A0000, no chain, no odd/even
    
    // Read font data from plane 2 at 0xA0000
    volatile uint8_t* font_mem = (volatile uint8_t*)0xA0000;
    for (int i = 0; i < VGA_FONT_SIZE; i++) {
        saved_font[i] = font_mem[i];
    }
    
    // Restore normal text mode read settings
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);  // Read Map Select = plane 0
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);  // Graphics Mode = odd/even
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);  // Misc Graphics = B8000, text mode
    
    font_saved = 1;
    serial_puts("Font saved successfully\n");
}

// Restore VGA font to plane 2 after returning to text mode
static void vga_restore_font(void) {
    if (!font_saved) {
        serial_puts("WARNING: No saved font to restore\n");
        return;
    }
    
    serial_puts("Restoring VGA font to plane 2...\n");
    
    // Set up to write to plane 2 only
    outb(0x3C4, 0x02);  // Map Mask register
    outb(0x3C5, 0x04);  // Write to plane 2 only
    
    outb(0x3C4, 0x04);  // Memory Mode register
    outb(0x3C5, 0x06);  // Sequential access, extended memory (disable chain-4, disable odd/even)
    
    outb(0x3CE, 0x05);  // Graphics Mode register
    outb(0x3CF, 0x00);  // Write mode 0, read mode 0
    
    outb(0x3CE, 0x06);  // Miscellaneous Graphics register
    outb(0x3CF, 0x04);  // Map at A0000, no chain, no odd/even
    
    // Write font data to plane 2 at 0xA0000
    volatile uint8_t* font_mem = (volatile uint8_t*)0xA0000;
    for (int i = 0; i < VGA_FONT_SIZE; i++) {
        font_mem[i] = saved_font[i];
    }
    
    // Restore normal text mode write settings
    outb(0x3C4, 0x02); outb(0x3C5, 0x03);  // Map Mask = planes 0,1 (char + attr)
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);  // Memory Mode = odd/even, no chain-4
    
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);  // Graphics Mode = odd/even
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);  // Misc Graphics = B8000, text mode
    
    serial_puts("Font restored successfully\n");
}

int vga_set_mode(uint16_t mode) {
    // Save mode info
    current_mode_info.mode_number = mode;
    
    if (mode == 0x03) {
        // Standard VGA text mode 80x25
        graphics_mode_enabled = 0;
        current_mode_info.type = VGA_MODE_TEXT;
        current_mode_info.width = 80;
        current_mode_info.height = 25;
        current_mode_info.bpp = 4;
        current_mode_info.framebuffer = VGA_ADDRESS;
        
        // Reset to standard text mode - complete register programming
        
        // Miscellaneous Output Register
        outb(0x3C2, 0x67);
        
        // Sequencer Registers
        outb(0x3C4, 0x00); outb(0x3C5, 0x03);  // Reset
        outb(0x3C4, 0x01); outb(0x3C5, 0x00);  // Clocking Mode - normal
        outb(0x3C4, 0x02); outb(0x3C5, 0x03);  // Map Mask - planes 0,1
        outb(0x3C4, 0x03); outb(0x3C5, 0x00);  // Character Map Select
        outb(0x3C4, 0x04); outb(0x3C5, 0x02);  // Memory Mode - odd/even, not chain-4
        
        // Unlock CRTC registers
        outb(0x3D4, 0x11);
        outb(0x3D5, inb(0x3D5) & ~0x80);
        
        // CRTC Registers for 80x25 text mode
        const uint8_t crtc_80x25[] = {
            0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,  // 0-7
            0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,  // 8-15 (cursor shape at 13-14)
            0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,  // 16-23
            0xFF                                              // 24
        };
        for (uint8_t i = 0; i < 25; i++) {
            outb(0x3D4, i);
            outb(0x3D5, crtc_80x25[i]);
        }
        
        // Graphics Controller Registers - text mode settings
        outb(0x3CE, 0x00); outb(0x3CF, 0x00);  // Set/Reset
        outb(0x3CE, 0x01); outb(0x3CF, 0x00);  // Enable Set/Reset
        outb(0x3CE, 0x02); outb(0x3CF, 0x00);  // Color Compare
        outb(0x3CE, 0x03); outb(0x3CF, 0x00);  // Data Rotate
        outb(0x3CE, 0x04); outb(0x3CF, 0x00);  // Read Map Select
        outb(0x3CE, 0x05); outb(0x3CF, 0x10);  // Graphics Mode - odd/even, read mode 0
        outb(0x3CE, 0x06); outb(0x3CF, 0x0E);  // Miscellaneous - B8000, text mode
        outb(0x3CE, 0x07); outb(0x3CF, 0x00);  // Color Don't Care
        outb(0x3CE, 0x08); outb(0x3CF, 0xFF);  // Bit Mask
        
        // Attribute Controller Registers
        inb(0x3DA);  // Reset flip-flop
        
        // Palette registers - identity mapping
        for (uint8_t i = 0; i < 16; i++) {
            outb(0x3C0, i);
            outb(0x3C0, i);
        }
        
        // Attribute controller mode settings
        outb(0x3C0, 0x10); outb(0x3C0, 0x0C);  // Mode Control - text, line graphics, blink
        outb(0x3C0, 0x11); outb(0x3C0, 0x00);  // Overscan Color
        outb(0x3C0, 0x12); outb(0x3C0, 0x0F);  // Color Plane Enable
        outb(0x3C0, 0x13); outb(0x3C0, 0x08);  // Horizontal Pixel Panning
        outb(0x3C0, 0x14); outb(0x3C0, 0x00);  // Color Select
        
        // Re-enable video output
        outb(0x3C0, 0x20);
        
        // Set standard 16-color text palette
        outb(0x3C8, 0);
        const uint8_t text_palette[][3] = {
            {0,0,0}, {0,0,42}, {0,42,0}, {0,42,42},
            {42,0,0}, {42,0,42}, {42,21,0}, {42,42,42},
            {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
            {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63}
        };
        for (uint8_t i = 0; i < 16; i++) {
            outb(0x3C9, text_palette[i][0]);
            outb(0x3C9, text_palette[i][1]);
            outb(0x3C9, text_palette[i][2]);
        }
        
        vga_buffer = (uint16_t*)VGA_ADDRESS;
        
        // CRITICAL: Restore the font data to plane 2
        // Mode 13h overwrites all planes including the character bitmaps
        vga_restore_font();
        
        // Remap VGA buffer to ensure page tables are correct after mode switch
        remap_vga_buffer();
        
        // Clear screen with proper text mode writes
        volatile uint16_t* buf = (volatile uint16_t*)VGA_ADDRESS;
        for (int i = 0; i < 80 * 25; i++) {
            buf[i] = 0x0720;  // Space, white on black
        }
        
        // Reset internal state
        vga_row = 0;
        vga_col = 0;
        vga_color = 0x0F;
        
        serial_puts("Text mode 0x03 fully restored\n");
        
        return 1;
    }
    
    if (mode == VGA_MODE_320x200x256) {
        // Save the font BEFORE switching to graphics mode
        if (!graphics_mode_enabled) {
            vga_save_font();
        }
        
        // Mode 13h - 320x200, 256 colors
        graphics_mode_enabled = 1;
        current_mode_info.type = VGA_MODE_GRAPHICS;
        current_mode_info.width = 320;
        current_mode_info.height = 200;
        current_mode_info.bpp = 8;
        current_mode_info.framebuffer = 0xA0000;
        current_mode_info.pitch = 320;
        current_mode_info.framebuffer_size = 320 * 200;
        current_mode_info.is_linear = 0;
        current_mode_info.is_vbe = 0;
        
        graphics_framebuffer = (uint8_t*)0xA0000;
        
        // Actually set mode 13h via VGA registers
        // This is the standard VGA mode 13h register programming
        
        // Write to Miscellaneous Output Register
        outb(0x3C2, 0x63);
        
        // Sequencer registers
        outb(0x3C4, 0x00); outb(0x3C5, 0x03);  // Reset
        outb(0x3C4, 0x01); outb(0x3C5, 0x01);  // Clocking mode
        outb(0x3C4, 0x02); outb(0x3C5, 0x0F);  // Map mask
        outb(0x3C4, 0x03); outb(0x3C5, 0x00);  // Character map
        outb(0x3C4, 0x04); outb(0x3C5, 0x0E);  // Memory mode - chain 4, no odd/even
        
        // CRTC registers - unlock
        outb(0x3D4, 0x11); outb(0x3D5, 0x00);
        
        // CRTC registers - timing for 320x200
        const uint8_t crtc_regs[] = {
            0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
            0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF
        };
        for (uint8_t i = 0; i < 25; i++) {
            outb(0x3D4, i);
            outb(0x3D5, crtc_regs[i]);
        }
        
        // Graphics Controller registers
        outb(0x3CE, 0x00); outb(0x3CF, 0x00);  // Set/Reset
        outb(0x3CE, 0x01); outb(0x3CF, 0x00);  // Enable Set/Reset
        outb(0x3CE, 0x02); outb(0x3CF, 0x00);  // Color Compare
        outb(0x3CE, 0x03); outb(0x3CF, 0x00);  // Data Rotate
        outb(0x3CE, 0x04); outb(0x3CF, 0x00);  // Read Map Select
        outb(0x3CE, 0x05); outb(0x3CF, 0x40);  // Graphics Mode - 256 color mode
        outb(0x3CE, 0x06); outb(0x3CF, 0x05);  // Misc - A0000 64k region
        outb(0x3CE, 0x07); outb(0x3CF, 0x0F);  // Color Don't Care
        outb(0x3CE, 0x08); outb(0x3CF, 0xFF);  // Bit Mask
        
        // Attribute Controller registers
        inb(0x3DA);  // Reset flip-flop
        for (uint8_t i = 0; i < 16; i++) {
            outb(0x3C0, i);
            outb(0x3C0, i);  // Palette registers - identity mapping
        }
        outb(0x3C0, 0x10); outb(0x3C0, 0x41);  // Mode control - graphics mode
        outb(0x3C0, 0x11); outb(0x3C0, 0x00);  // Overscan color
        outb(0x3C0, 0x12); outb(0x3C0, 0x0F);  // Color Plane Enable
        outb(0x3C0, 0x13); outb(0x3C0, 0x00);  // Horizontal Pixel Panning
        outb(0x3C0, 0x14); outb(0x3C0, 0x00);  // Color Select
        outb(0x3C0, 0x20);  // Re-enable video
        
        // Set standard 256-color palette
        outb(0x3C8, 0);  // Start at color 0
        for (uint16_t color = 0; color < 256; color++) {
            // Generate 6-6-6 RGB palette (default VGA 256-color palette)
            uint8_t r, g, b;
            if (color < 16) {
                // First 16 colors - standard VGA palette
                const uint8_t vga16[][3] = {
                    {0,0,0}, {0,0,42}, {0,42,0}, {0,42,42},
                    {42,0,0}, {42,0,42}, {42,21,0}, {42,42,42},
                    {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
                    {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63}
                };
                r = vga16[color][0];
                g = vga16[color][1];
                b = vga16[color][2];
            } else {
                // 216-color cube (6x6x6) + 24 grayscale
                if (color < 232) {
                    uint8_t idx = color - 16;
                    r = ((idx / 36) % 6) * 12;
                    g = ((idx / 6) % 6) * 12;
                    b = (idx % 6) * 12;
                } else {
                    // Grayscale ramp
                    uint8_t gray = ((color - 232) * 63) / 23;
                    r = g = b = gray;
                }
            }
            outb(0x3C9, r);  // Red
            outb(0x3C9, g);  // Green
            outb(0x3C9, b);  // Blue
        }
        
        serial_puts("Mode 13h hardware configured\n");
        return 1;
    }
    
    // VBE modes
    if (mode >= 0x100) {
        if (!vbe_available) return 0;

        vga_mode_info_t previous_mode_info = current_mode_info;
        uint8_t* previous_framebuffer = graphics_framebuffer;
        int previous_graphics_enabled = graphics_mode_enabled;
        
        vbe_mode_info_t mode_info;
        if (!vga_get_vbe_mode_info(mode, &mode_info)) {
            return 0;
        }
        
        graphics_mode_enabled = 1;
        current_mode_info.type = VGA_MODE_GRAPHICS;
        current_mode_info.width = mode_info.width;
        current_mode_info.height = mode_info.height;
        current_mode_info.bpp = mode_info.bpp;
        current_mode_info.framebuffer = mode_info.framebuffer;
        current_mode_info.pitch = mode_info.pitch;
        current_mode_info.framebuffer_size = mode_info.pitch * mode_info.height;
        current_mode_info.is_linear = 1;
        current_mode_info.is_vbe = 1;
        
        graphics_framebuffer = (uint8_t*)(uint32_t)mode_info.framebuffer;
        
        // VBE Function 02h: Set VBE Mode
        // INT 0x10, AX=0x4F02, BX=mode | 0x4000 (enable linear framebuffer bit)
        v86_regs_t regs;
        memset(&regs, 0, sizeof(regs));
        
        regs.eax = VBE_FUNCTION_SET_MODE;  // 0x4F02
        // Set bit 14 (0x4000) for linear framebuffer, bit 15 (0x8000) to preserve memory
        regs.ebx = mode | 0x4000;
        
        // Execute VBE mode set
        int result = vga_bios_int10(&regs);
        
        if (result == 0 && (regs.eax & 0xFFFF) == 0x004F) {
            serial_puts("VBE mode set successfully via INT 0x10\n");
            
            // Map framebuffer memory if needed
            if (mode_info.framebuffer >= 0xE0000000) {
                // This is a high memory framebuffer, needs proper page mapping
                // For now, trust that it's accessible
                serial_puts("Linear framebuffer at high memory\n");
            }
            
            return 1;
        } else {
            serial_puts("VBE mode set via BIOS failed\n");
            current_mode_info = previous_mode_info;
            graphics_framebuffer = previous_framebuffer;
            graphics_mode_enabled = previous_graphics_enabled;
            return 0;
        }
    }
    
    return 0;
}

int vga_get_current_mode(void) {
    // If in VBE mode, query BIOS for current mode
    // VBE Function 03h: Get Current VBE Mode
    // INT 0x10, AX=0x4F03, returns BX=current mode
    if (vbe_available && graphics_mode_enabled) {
        v86_regs_t regs;
        memset(&regs, 0, sizeof(regs));
        
        regs.eax = VBE_FUNCTION_GET_MODE;  // 0x4F03
        
        int result = vga_bios_int10(&regs);
        
        if (result == 0 && (regs.eax & 0xFFFF) == 0x004F) {
            // BX contains the current mode
            uint16_t bios_mode = regs.ebx & 0xFFFF;
            // Remove linear framebuffer bit if present
            uint16_t mode = bios_mode & ~0x4000;
            
            // Update cached mode info if different
            if (mode != current_mode_info.mode_number) {
                current_mode_info.mode_number = mode;
            }
            
            return mode;
        }
    }
    
    return current_mode_info.mode_number;
}

vga_mode_info_t* vga_get_mode_info(void) {
    return &current_mode_info;
}

void vga_list_available_modes(void) {
    serial_puts("Available VGA Modes:\n");
    serial_puts("  0x03: 80x25 Text Mode (16 colors)\n");
    serial_puts("  0x13: 320x200 Graphics (256 colors)\n");
    
    if (vbe_available) {
        serial_puts("VBE Modes:\n");
        serial_puts("  0x101: 640x480x256\n");
        serial_puts("  0x103: 800x600x256\n");
        serial_puts("  0x105: 1024x768x256\n");
        serial_puts("  0x112: 640x480x16M (24-bit)\n");
        serial_puts("  0x115: 800x600x16M (24-bit)\n");
        serial_puts("  0x118: 1024x768x16M (24-bit)\n");
    }
}

// VBE Function 09h: Set Palette Data
// Sets palette entries for 8-bit color modes
int vga_vbe_set_palette(uint16_t first_entry, uint16_t num_entries, const rgb_color_t* palette_data) {
    if (!vbe_available || !palette_data || num_entries == 0) return 0;
    
    // VBE Function 09h, Sub-function 00h: Set Palette Data
    // INT 0x10, AX=0x4F09, BL=00h (set), CX=num_entries, DX=first_entry
    // ES:DI=pointer to palette data (array of R,G,B,pad)
    
    v86_regs_t regs;
    memset(&regs, 0, sizeof(regs));
    
    regs.eax = VBE_FUNCTION_SET_PALETTE;  // 0x4F09
    regs.ebx = 0x00;                       // BL=00h: set palette
    regs.ecx = num_entries;                // Number of entries to set
    regs.edx = first_entry;                // First entry to set
    
    // Copy palette data to real mode buffer
    // VBE palette format: 4 bytes per entry (R, G, B, reserved)
    volatile uint8_t* buffer = (volatile uint8_t*)REAL_MODE_BUFFER;
    for (uint16_t i = 0; i < num_entries && i < 256; i++) {
        // VBE uses 6-bit color values (0-63) for compatibility
        buffer[i * 4 + 0] = palette_data[i].r >> 2;  // Red (6-bit)
        buffer[i * 4 + 1] = palette_data[i].g >> 2;  // Green (6-bit)
        buffer[i * 4 + 2] = palette_data[i].b >> 2;  // Blue (6-bit)
        buffer[i * 4 + 3] = 0;                       // Reserved
    }
    
    // Set ES:DI to point to palette buffer
    regs.es = (REAL_MODE_BUFFER >> 4);
    regs.edi = (REAL_MODE_BUFFER & 0x0F);
    
    int result = vga_bios_int10(&regs);
    
    if (result == 0 && (regs.eax & 0xFFFF) == 0x004F) {
        return 1;  // Success
    }
    
    // Fallback: Use direct VGA DAC programming for standard 256-color modes
    outb(0x3C8, first_entry);  // DAC write index
    for (uint16_t i = 0; i < num_entries && (first_entry + i) < 256; i++) {
        outb(0x3C9, palette_data[i].r >> 2);  // Red (6-bit)
        outb(0x3C9, palette_data[i].g >> 2);  // Green (6-bit)
        outb(0x3C9, palette_data[i].b >> 2);  // Blue (6-bit)
    }
    
    return 1;
}

// VBE Function 09h, Sub-function 01h: Get Palette Data
int vga_vbe_get_palette(uint16_t first_entry, uint16_t num_entries, rgb_color_t* palette_data) {
    if (!vbe_available || !palette_data || num_entries == 0) return 0;
    
    v86_regs_t regs;
    memset(&regs, 0, sizeof(regs));
    
    regs.eax = VBE_FUNCTION_SET_PALETTE;  // 0x4F09
    regs.ebx = 0x01;                       // BL=01h: get palette
    regs.ecx = num_entries;
    regs.edx = first_entry;
    
    regs.es = (REAL_MODE_BUFFER >> 4);
    regs.edi = (REAL_MODE_BUFFER & 0x0F);
    
    int result = vga_bios_int10(&regs);
    
    if (result == 0 && (regs.eax & 0xFFFF) == 0x004F) {
        // Copy palette data from real mode buffer
        volatile uint8_t* buffer = (volatile uint8_t*)REAL_MODE_BUFFER;
        for (uint16_t i = 0; i < num_entries; i++) {
            // Convert from 6-bit to 8-bit color values
            palette_data[i].r = buffer[i * 4 + 0] << 2;
            palette_data[i].g = buffer[i * 4 + 1] << 2;
            palette_data[i].b = buffer[i * 4 + 2] << 2;
        }
        return 1;
    }
    
    // Fallback: Read directly from VGA DAC
    outb(0x3C7, first_entry);  // DAC read index
    for (uint16_t i = 0; i < num_entries && (first_entry + i) < 256; i++) {
        palette_data[i].r = inb(0x3C9) << 2;  // Read red (6-bit -> 8-bit)
        palette_data[i].g = inb(0x3C9) << 2;  // Read green
        palette_data[i].b = inb(0x3C9) << 2;  // Read blue
    }
    
    return 1;
}


// COLOR CONVERSION & HEX COLOR SUPPORT


// Helper: Parse hex digit
static uint8_t parse_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// Convert hex color string to RGB (e.g., "#FF00FF" or "FF00FF")
rgb_color_t vga_hex_to_rgb(const char* hex) {
    rgb_color_t rgb = {0, 0, 0};
    
    if (!hex) return rgb;
    
    // Skip '#' if present
    if (hex[0] == '#') hex++;
    
    // Parse RRGGBB format
    if (vga_strlen(hex) >= 6) {
        rgb.r = (parse_hex_digit(hex[0]) << 4) | parse_hex_digit(hex[1]);
        rgb.g = (parse_hex_digit(hex[2]) << 4) | parse_hex_digit(hex[3]);
        rgb.b = (parse_hex_digit(hex[4]) << 4) | parse_hex_digit(hex[5]);
    }
    
    return rgb;
}

// Find closest VGA 16-color palette entry for given RGB
static uint8_t vga_find_closest_color(uint8_t r, uint8_t g, uint8_t b) {
    // VGA 16-color palette approximations (in RGB)
    static const uint8_t palette[][3] = {
        {0, 0, 0},       // Black
        {0, 0, 170},     // Blue
        {0, 170, 0},     // Green
        {0, 170, 170},   // Cyan
        {170, 0, 0},     // Red
        {170, 0, 170},   // Magenta
        {170, 85, 0},    // Brown
        {170, 170, 170}, // Light Gray
        {85, 85, 85},    // Dark Gray
        {85, 85, 255},   // Light Blue
        {85, 255, 85},   // Light Green
        {85, 255, 255},  // Light Cyan
        {255, 85, 85},   // Light Red
        {255, 85, 255},  // Light Magenta
        {255, 255, 85},  // Yellow
        {255, 255, 255}  // White
    };
    
    uint8_t closest = 0;
    uint32_t min_dist = 0xFFFFFFFF;
    
    for (uint8_t i = 0; i < 16; i++) {
        int dr = (int)r - palette[i][0];
        int dg = (int)g - palette[i][1];
        int db = (int)b - palette[i][2];
        uint32_t dist = dr*dr + dg*dg + db*db;
        
        if (dist < min_dist) {
            min_dist = dist;
            closest = i;
        }
    }
    
    return closest;
}

uint8_t vga_rgb_to_vga_color(rgb_color_t rgb) {
    return vga_find_closest_color(rgb.r, rgb.g, rgb.b);
}

uint8_t vga_rgb_to_256_palette(rgb_color_t rgb) {
    // 256-color palette mapping (6x6x6 color cube + grayscale)
    // Colors 0-15: Standard VGA colors
    // Colors 16-231: 6x6x6 RGB cube
    // Colors 232-255: Grayscale ramp
    
    // Use 6x6x6 color cube
    uint8_t r6 = (rgb.r * 6) / 256;
    uint8_t g6 = (rgb.g * 6) / 256;
    uint8_t b6 = (rgb.b * 6) / 256;
    
    return 16 + (r6 * 36) + (g6 * 6) + b6;
}

rgb565_t vga_rgb_to_rgb565(rgb_color_t rgb) {
    // Convert 8-bit RGB to 5:6:5 format
    uint16_t r5 = (rgb.r >> 3) & 0x1F;
    uint16_t g6 = (rgb.g >> 2) & 0x3F;
    uint16_t b5 = (rgb.b >> 3) & 0x1F;
    
    return (r5 << 11) | (g6 << 5) | b5;
}

rgb555_t vga_rgb_to_rgb555(rgb_color_t rgb) {
    // Convert 8-bit RGB to 5:5:5 format
    uint16_t r5 = (rgb.r >> 3) & 0x1F;
    uint16_t g5 = (rgb.g >> 3) & 0x1F;
    uint16_t b5 = (rgb.b >> 3) & 0x1F;
    
    return (r5 << 10) | (g5 << 5) | b5;
}

uint32_t vga_rgb_to_rgb888(rgb_color_t rgb) {
    // Convert to 24-bit RGB (stored in 32-bit)
    return ((uint32_t)rgb.r << 16) | ((uint32_t)rgb.g << 8) | rgb.b;
}

rgb_color_t vga_rgb565_to_rgb(rgb565_t color) {
    rgb_color_t rgb;
    rgb.r = ((color >> 11) & 0x1F) << 3;
    rgb.g = ((color >> 5) & 0x3F) << 2;
    rgb.b = (color & 0x1F) << 3;
    return rgb;
}

rgb_color_t vga_rgb555_to_rgb(rgb555_t color) {
    rgb_color_t rgb;
    rgb.r = ((color >> 10) & 0x1F) << 3;
    rgb.g = ((color >> 5) & 0x1F) << 3;
    rgb.b = (color & 0x1F) << 3;
    return rgb;
}

rgb_color_t vga_rgb888_to_rgb(uint32_t color) {
    rgb_color_t rgb;
    rgb.r = (color >> 16) & 0xFF;
    rgb.g = (color >> 8) & 0xFF;
    rgb.b = color & 0xFF;
    return rgb;
}

rgb_color_t vga_vga_color_to_rgb(uint8_t vga_color) {
    // Convert VGA 16-color palette to approximate RGB
    static const uint8_t palette[][3] = {
        {0, 0, 0},       {0, 0, 170},     {0, 170, 0},     {0, 170, 170},
        {170, 0, 0},     {170, 0, 170},   {170, 85, 0},    {170, 170, 170},
        {85, 85, 85},    {85, 85, 255},   {85, 255, 85},   {85, 255, 255},
        {255, 85, 85},   {255, 85, 255},  {255, 255, 85},  {255, 255, 255}
    };
    
    rgb_color_t rgb;
    uint8_t idx = vga_color & 0x0F;
    rgb.r = palette[idx][0];
    rgb.g = palette[idx][1];
    rgb.b = palette[idx][2];
    return rgb;
}

rgb_color_t vga_blend_rgb(rgb_color_t fg, rgb_color_t bg, uint8_t alpha) {
    rgb_color_t result;
    result.r = ((fg.r * alpha) + (bg.r * (255 - alpha))) / 255;
    result.g = ((fg.g * alpha) + (bg.g * (255 - alpha))) / 255;
    result.b = ((fg.b * alpha) + (bg.b * (255 - alpha))) / 255;
    return result;
}

rgba_color_t vga_blend_rgba(rgba_color_t fg, rgba_color_t bg) {
    rgba_color_t result;
    uint8_t alpha = fg.alpha;
    result.r = ((fg.r * alpha) + (bg.r * (255 - alpha))) / 255;
    result.g = ((fg.g * alpha) + (bg.g * (255 - alpha))) / 255;
    result.b = ((fg.b * alpha) + (bg.b * (255 - alpha))) / 255;
    result.alpha = fg.alpha + ((255 - fg.alpha) * bg.alpha) / 255;
    return result;
}

// GRAPHICS MODE PIXEL-LEVEL DRAWING


void vga_plot_pixel(uint16_t x, uint16_t y, uint32_t color) {
    if (!graphics_mode_enabled || !graphics_framebuffer) return;
    if (x >= current_mode_info.width || y >= current_mode_info.height) return;
    
    uint8_t* buffer = double_buffer_enabled && back_buffer ? back_buffer : graphics_framebuffer;
    uint32_t offset = y * current_mode_info.pitch + x * (current_mode_info.bpp / 8);
    
    switch (current_mode_info.bpp) {
        case 8:
            buffer[offset] = (uint8_t)color;
            break;
        case 16:
            *(uint16_t*)(buffer + offset) = (uint16_t)color;
            break;
        case 24:
            buffer[offset] = color & 0xFF;
            buffer[offset + 1] = (color >> 8) & 0xFF;
            buffer[offset + 2] = (color >> 16) & 0xFF;
            break;
        case 32:
            *(uint32_t*)(buffer + offset) = color;
            break;
    }
}

uint32_t vga_get_pixel(uint16_t x, uint16_t y) {
    if (!graphics_mode_enabled || !graphics_framebuffer) return 0;
    if (x >= current_mode_info.width || y >= current_mode_info.height) return 0;
    
    uint8_t* buffer = graphics_framebuffer;
    uint32_t offset = y * current_mode_info.pitch + x * (current_mode_info.bpp / 8);
    
    switch (current_mode_info.bpp) {
        case 8:
            return buffer[offset];
        case 16:
            return *(uint16_t*)(buffer + offset);
        case 24:
            return buffer[offset] | (buffer[offset + 1] << 8) | (buffer[offset + 2] << 16);
        case 32:
            return *(uint32_t*)(buffer + offset);
        default:
            return 0;
    }
}

void vga_clear_screen(uint32_t color) {
    if (!graphics_mode_enabled) return;
    
    for (uint16_t y = 0; y < current_mode_info.height; y++) {
        for (uint16_t x = 0; x < current_mode_info.width; x++) {
            vga_plot_pixel(x, y, color);
        }
    }
}


// LINE & SHAPE DRAWING


void vga_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color) {
    int dx = abs((int)x1 - x0);
    int dy = abs((int)y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        vga_plot_pixel(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void vga_draw_line_thick(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color, uint8_t thickness) {
    if (thickness == 1) {
        vga_draw_line(x0, y0, x1, y1, color);
        return;
    }
    
    for (int t = -(int)thickness/2; t <= (int)thickness/2; t++) {
        vga_draw_line(x0, y0 + t, x1, y1 + t, color);
        vga_draw_line(x0 + t, y0, x1 + t, y1, color);
    }
}

void vga_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color) {
    vga_draw_line(x, y, x + width - 1, y, color);
    vga_draw_line(x, y + height - 1, x + width - 1, y + height - 1, color);
    vga_draw_line(x, y, x, y + height - 1, color);
    vga_draw_line(x + width - 1, y, x + width - 1, y + height - 1, color);
}

void vga_fill_rect_gfx(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color) {
    for (uint16_t row = y; row < y + height && row < current_mode_info.height; row++) {
        for (uint16_t col = x; col < x + width && col < current_mode_info.width; col++) {
            vga_plot_pixel(col, row, color);
        }
    }
}

void vga_draw_rounded_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t radius, uint32_t color) {
    vga_draw_line(x + radius, y, x + width - radius, y,  color);
    vga_draw_line(x + radius, y + height, x + width - radius, y + height, color);
    vga_draw_line(x, y + radius, x, y + height - radius, color);
    vga_draw_line(x + width, y + radius, x + width, y + height - radius, color);
    
    for (uint8_t i = 0; i < radius; i++) {
        uint8_t offset = radius - i;
        vga_plot_pixel(x + i, y + offset, color);
        vga_plot_pixel(x + width - i, y + offset, color);
        vga_plot_pixel(x + i, y + height - offset, color);
        vga_plot_pixel(x + width - i, y + height - offset, color);
    }
}


// CIRCLE & ELLIPSE DRAWING


void vga_draw_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint32_t color) {
    int x = 0, y = radius, d = 1 - radius;
    
    while (x <= y) {
        vga_plot_pixel(cx + x, cy + y, color);
        vga_plot_pixel(cx - x, cy + y, color);
        vga_plot_pixel(cx + x, cy - y, color);
        vga_plot_pixel(cx - x, cy - y, color);
        vga_plot_pixel(cx + y, cy + x, color);
        vga_plot_pixel(cx - y, cy + x, color);
        vga_plot_pixel(cx + y, cy - x, color);
        vga_plot_pixel(cx - y, cy - x, color);
        
        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

void vga_fill_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint32_t color) {
    for (int y = -(int)radius; y <= (int)radius; y++) {
        for (int x = -(int)radius; x <= (int)radius; x++) {
            if (x*x + y*y <= (int)radius * (int)radius) {
                vga_plot_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void vga_draw_ellipse(uint16_t cx, uint16_t cy, uint16_t rx, uint16_t ry, uint32_t color) {
    int x = 0, y = ry;
    int rx2 = rx * rx, ry2 = ry * ry;
    int two_rx2 = 2 * rx2, two_ry2 = 2 * ry2;
    int p = ry2 - (rx2 * ry) + (rx2 / 4);
    int px = 0, py = two_rx2 * y;
    
    while (px < py) {
        vga_plot_pixel(cx + x, cy + y, color);
        vga_plot_pixel(cx - x, cy + y, color);
        vga_plot_pixel(cx + x, cy - y, color);
        vga_plot_pixel(cx - x, cy - y, color);
        
        x++; px += two_ry2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--; py -= two_rx2;
            p += ry2 + px - py;
        }
    }
    
    p = ry2 * (x + 1) * (x + 1) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        vga_plot_pixel(cx + x, cy + y, color);
        vga_plot_pixel(cx - x, cy + y, color);
        vga_plot_pixel(cx + x, cy - y, color);
        vga_plot_pixel(cx - x, cy - y, color);
        
        y--; py -= two_rx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++; px += two_ry2;
            p += rx2 - py + px;
        }
    }
}

void vga_fill_ellipse(uint16_t cx, uint16_t cy, uint16_t rx, uint16_t ry, uint32_t color) {
    for (int y = -(int)ry; y <= (int)ry; y++) {
        for (int x = -(int)rx; x <= (int)rx; x++) {
            if ((x*x*ry*ry + y*y*rx*rx) <= (int)(rx*rx*ry*ry)) {
                vga_plot_pixel(cx + x, cy + y, color);
            }
        }
    }
}


// TRIANGLE & POLYGON DRAWING


void vga_draw_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color) {
    vga_draw_line(x0, y0, x1, y1, color);
    vga_draw_line(x1, y1, x2, y2, color);
    vga_draw_line(x2, y2, x0, y0, color);
}

void vga_fill_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color) {
    if (y0 > y1) { swap((int*)&y0, (int*)&y1); swap((int*)&x0, (int*)&x1); }
    if (y1 > y2) { swap((int*)&y1, (int*)&y2); swap((int*)&x1, (int*)&x2); }
    if (y0 > y1) { swap((int*)&y0, (int*)&y1); swap((int*)&x0, (int*)&x1); }
    
    for (uint16_t y = y0; y <= y2; y++) {
        int x_start, x_end;
        
        if (y < y1) {
            x_start = x0 + ((x1 - x0) * (y - y0)) / (y1 - y0 + 1);
            x_end = x0 + ((x2 - x0) * (y - y0)) / (y2 - y0 + 1);
        } else {
            x_start = x1 + ((x2 - x1) * (y - y1)) / (y2 - y1 + 1);
            x_end = x0 + ((x2 - x0) * (y - y0)) / (y2 - y0 + 1);
        }
        
        if (x_start > x_end) swap(&x_start, &x_end);
        
        for (int x = x_start; x <= x_end; x++) {
            vga_plot_pixel(x, y, color);
        }
    }
}

void vga_draw_polygon(uint16_t* points, uint16_t num_points, uint32_t color) {
    for (uint16_t i = 0; i < num_points; i++) {
        uint16_t x0 = points[i * 2];
        uint16_t y0 = points[i * 2 + 1];
        uint16_t x1 = points[((i + 1) % num_points) * 2];
        uint16_t y1 = points[((i + 1) % num_points) * 2 + 1];
        vga_draw_line(x0, y0, x1, y1, color);
    }
}

void vga_fill_polygon(uint16_t* points, uint16_t num_points, uint32_t color) {
    if (num_points < 3) return;
    
    for (uint16_t i = 1; i < num_points - 1; i++) {
        vga_fill_triangle(
            points[0], points[1],
            points[i * 2], points[i * 2 + 1],
            points[(i + 1) * 2], points[(i + 1) * 2 + 1],
            color
        );
    }
}


// BITMAP & SPRITE OPERATIONS


void vga_draw_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* bitmap) {
    if (!bitmap) return;
    
    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t col = 0; col < width; col++) {
            uint32_t color = bitmap[row * width + col];
            vga_plot_pixel(x + col, y + row, color);
        }
    }
}

void vga_draw_bitmap_alpha(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* bitmap, uint8_t alpha) {
    if (!bitmap) return;
    
    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t col = 0; col < width; col++) {
            uint32_t fg_color = bitmap[row * width + col];
            uint32_t bg_color = vga_get_pixel(x + col, y + row);
            
            rgb_color_t fg = vga_rgb888_to_rgb(fg_color);
            rgb_color_t bg = vga_rgb888_to_rgb(bg_color);
            rgb_color_t blended = vga_blend_rgb(fg, bg, alpha);
            
            vga_plot_pixel(x + col, y + row, vga_rgb_to_rgb888(blended));
        }
    }
}

void vga_blit(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height) {
    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t col = 0; col < width; col++) {
            uint32_t color = vga_get_pixel(src_x + col, src_y + row);
            vga_plot_pixel(dst_x + col, dst_y + row, color);
        }
    }
}

void vga_blit_scaled(uint16_t src_x, uint16_t src_y, uint16_t src_w, uint16_t src_h, 
                     uint16_t dst_x, uint16_t dst_y, uint16_t dst_w, uint16_t dst_h) {
    for (uint16_t row = 0; row < dst_h; row++) {
        for (uint16_t col = 0; col < dst_w; col++) {
            uint16_t sx = src_x + (col * src_w) / dst_w;
            uint16_t sy = src_y + (row * src_h) / dst_h;
            uint32_t color = vga_get_pixel(sx, sy);
            vga_plot_pixel(dst_x + col, dst_y + row, color);
        }
    }
}

void vga_draw_sprite(uint16_t x, uint16_t y, const vga_sprite_t* sprite) {
    if (!sprite || !sprite->data) return;
    vga_draw_bitmap(x, y, sprite->width, sprite->height, sprite->data);
}

void vga_draw_sprite_transparent(uint16_t x, uint16_t y, const vga_sprite_t* sprite, uint32_t transparent_color) {
    if (!sprite || !sprite->data) return;
    
    for (uint16_t row = 0; row < sprite->height; row++) {
        for (uint16_t col = 0; col < sprite->width; col++) {
            uint32_t color = sprite->data[row * sprite->width + col];
            if (color != transparent_color) {
                vga_plot_pixel(x + col, y + row, color);
            }
        }
    }
}


// PALETTE MANAGEMENT


void vga_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, index);
    outb(0x3C9, r >> 2);
    outb(0x3C9, g >> 2);
    outb(0x3C9, b >> 2);
}

void vga_get_palette_entry(uint8_t index, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (!r || !g || !b) return;
    
    outb(0x3C7, index);
    *r = inb(0x3C9) << 2;
    *g = inb(0x3C9) << 2;
    *b = inb(0x3C9) << 2;
}

void vga_set_palette(const uint8_t* palette, uint16_t count) {
    if (!palette) return;
    
    for (uint16_t i = 0; i < count && i < 256; i++) {
        vga_set_palette_entry(i, palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]);
    }
}

void vga_fade_palette_to_black(uint8_t steps) {
    for (uint8_t step = 0; step < steps; step++) {
        for (uint16_t i = 0; i < 256; i++) {
            uint8_t r, g, b;
            vga_get_palette_entry(i, &r, &g, &b);
            r = (r * (steps - step)) / steps;
            g = (g * (steps - step)) / steps;
            b = (b * (steps - step)) / steps;
            vga_set_palette_entry(i, r, g, b);
        }
    }
}

void vga_fade_palette_to_white(uint8_t steps) {
    for (uint8_t step = 0; step < steps; step++) {
        for (uint16_t i = 0; i < 256; i++) {
            uint8_t r, g, b;
            vga_get_palette_entry(i, &r, &g, &b);
            r = r + ((255 - r) * step) / steps;
            g = g + ((255 - g) * step) / steps;
            b = b + ((255 - b) * step) / steps;
            vga_set_palette_entry(i, r, g, b);
        }
    }
}

void vga_rotate_palette(uint8_t start, uint8_t end) {
    uint8_t r, g, b;
    vga_get_palette_entry(start, &r, &g, &b);
    
    for (uint8_t i = start; i < end; i++) {
        uint8_t nr, ng, nb;
        vga_get_palette_entry(i + 1, &nr, &ng, &nb);
        vga_set_palette_entry(i, nr, ng, nb);
    }
    
    vga_set_palette_entry(end, r, g, b);
}


// ADVANCED FEATURES


void vga_enable_double_buffer(void) {
    if (!graphics_mode_enabled) return;
    double_buffer_enabled = 1;
}

void vga_disable_double_buffer(void) {
    double_buffer_enabled = 0;
}

void vga_swap_buffers(void) {
    if (!double_buffer_enabled || !back_buffer || !graphics_framebuffer) return;
    
    uint32_t size = current_mode_info.framebuffer_size;
    for (uint32_t i = 0; i < size; i++) {
        graphics_framebuffer[i] = back_buffer[i];
    }
}

void vga_wait_vsync(void) {
    while (inb(0x3DA) & 0x08);
    while (!(inb(0x3DA) & 0x08));
}

void vga_enable_page_flipping(void) {
    // Implementation depends on specific VGA mode
}

void vga_flip_page(void) {
    vga_wait_vsync();
    vga_swap_buffers();
}

void* vga_get_framebuffer(void) {
    return graphics_framebuffer;
}

uint32_t vga_get_framebuffer_size(void) {
    return current_mode_info.framebuffer_size;
}

uint16_t vga_get_pitch(void) {
    return current_mode_info.pitch;
}

void vga_copy_to_framebuffer(const void* data, uint32_t size) {
    if (!graphics_framebuffer || !data) return;
    
    uint32_t copy_size = size < current_mode_info.framebuffer_size ? size : current_mode_info.framebuffer_size;
    for (uint32_t i = 0; i < copy_size; i++) {
        graphics_framebuffer[i] = ((uint8_t*)data)[i];
    }
}


// IMAGE FILTERS & EFFECTS


void vga_apply_filter_grayscale(void) {
    if (!graphics_mode_enabled) return;
    
    for (uint16_t y = 0; y < current_mode_info.height; y++) {
        for (uint16_t x = 0; x < current_mode_info.width; x++) {
            uint32_t color = vga_get_pixel(x, y);
            rgb_color_t rgb = vga_rgb888_to_rgb(color);
            
            uint8_t gray = (rgb.r * 30 + rgb.g * 59 + rgb.b * 11) / 100;
            rgb.r = rgb.g = rgb.b = gray;
            
            vga_plot_pixel(x, y, vga_rgb_to_rgb888(rgb));
        }
    }
}

void vga_apply_filter_sepia(void) {
    if (!graphics_mode_enabled) return;
    
    for (uint16_t y = 0; y < current_mode_info.height; y++) {
        for (uint16_t x = 0; x < current_mode_info.width; x++) {
            uint32_t color = vga_get_pixel(x, y);
            rgb_color_t rgb = vga_rgb888_to_rgb(color);
            
            uint8_t tr = (rgb.r * 39 + rgb.g * 77 + rgb.b * 19) / 100;
            uint8_t tg = (rgb.r * 35 + rgb.g * 69 + rgb.b * 17) / 100;
            uint8_t tb = (rgb.r * 27 + rgb.g * 53 + rgb.b * 13) / 100;
            
            rgb.r = tr > 255 ? 255 : tr;
            rgb.g = tg > 255 ? 255 : tg;
            rgb.b = tb > 255 ? 255 : tb;
            
            vga_plot_pixel(x, y, vga_rgb_to_rgb888(rgb));
        }
    }
}

void vga_apply_filter_invert(void) {
    if (!graphics_mode_enabled) return;
    
    for (uint16_t y = 0; y < current_mode_info.height; y++) {
        for (uint16_t x = 0; x < current_mode_info.width; x++) {
            uint32_t color = vga_get_pixel(x, y);
            rgb_color_t rgb = vga_rgb888_to_rgb(color);
            
            rgb.r = 255 - rgb.r;
            rgb.g = 255 - rgb.g;
            rgb.b = 255 - rgb.b;
            
            vga_plot_pixel(x, y, vga_rgb_to_rgb888(rgb));
        }
    }
}

void vga_apply_filter_blur(uint8_t radius) {
    if (!graphics_mode_enabled || radius == 0) return;
    
    for (uint16_t y = radius; y < current_mode_info.height - radius; y++) {
        for (uint16_t x = radius; x < current_mode_info.width - radius; x++) {
            uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
            uint32_t count = 0;
            
            for (int dy = -(int)radius; dy <= (int)radius; dy++) {
                for (int dx = -(int)radius; dx <= (int)radius; dx++) {
                    uint32_t color = vga_get_pixel(x + dx, y + dy);
                    rgb_color_t rgb = vga_rgb888_to_rgb(color);
                    sum_r += rgb.r;
                    sum_g += rgb.g;
                    sum_b += rgb.b;
                    count++;
                }
            }
            
            rgb_color_t avg;
            avg.r = sum_r / count;
            avg.g = sum_g / count;
            avg.b = sum_b / count;
            
            vga_plot_pixel(x, y, vga_rgb_to_rgb888(avg));
        }
    }
}

void vga_apply_gamma_correction(float gamma) {
    if (!graphics_mode_enabled) return;
    (void)gamma;
    
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        lut[i] = i;
    }
    
    for (uint16_t y = 0; y < current_mode_info.height; y++) {
        for (uint16_t x = 0; x < current_mode_info.width; x++) {
            uint32_t color = vga_get_pixel(x, y);
            rgb_color_t rgb = vga_rgb888_to_rgb(color);
            
            rgb.r = lut[rgb.r];
            rgb.g = lut[rgb.g];
            rgb.b = lut[rgb.b];
            
            vga_plot_pixel(x, y, vga_rgb_to_rgb888(rgb));
        }
    }
}
