/*
 * === AOS HEADER BEGIN ===
 * include/vga.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.8
 * === AOS HEADER END ===
 */


#ifndef VGA_H
#define VGA_H

#include <stdint.h>


// TEXT MODE DEFINITIONS


#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

// Text Mode Resolutions
#define VGA_TEXT_80x25  0
#define VGA_TEXT_80x50  1
#define VGA_TEXT_90x30  2
#define VGA_TEXT_90x60  3
#define VGA_TEXT_40x25  4


// GRAPHICS MODE DEFINITIONS


// Standard VGA Graphics Modes
#define VGA_MODE_320x200x256    0x13  // Mode 13h - 320x200, 256 colors
#define VGA_MODE_640x480x16     0x12  // Mode 12h - 640x480, 16 colors

// VESA VBE Modes (Linear Framebuffer)
#define VBE_MODE_640x480x16     0x111
#define VBE_MODE_640x480x256    0x101
#define VBE_MODE_800x600x16     0x114
#define VBE_MODE_800x600x256    0x103
#define VBE_MODE_1024x768x16    0x117
#define VBE_MODE_1024x768x256   0x105
#define VBE_MODE_1280x1024x16   0x11A
#define VBE_MODE_1280x1024x256  0x107

// True Color VESA Modes (15/16/24/32 bit)
#define VBE_MODE_640x480x32K    0x110  // 15-bit (5:5:5)
#define VBE_MODE_640x480x64K    0x111  // 16-bit (5:6:5)
#define VBE_MODE_640x480x16M    0x112  // 24-bit (8:8:8)
#define VBE_MODE_800x600x32K    0x113
#define VBE_MODE_800x600x64K    0x114
#define VBE_MODE_800x600x16M    0x115
#define VBE_MODE_1024x768x32K   0x116
#define VBE_MODE_1024x768x64K   0x117
#define VBE_MODE_1024x768x16M   0x118
#define VBE_MODE_1280x1024x32K  0x119
#define VBE_MODE_1280x1024x64K  0x11A
#define VBE_MODE_1280x1024x16M  0x11B

// VBE Function Codes
#define VBE_FUNCTION_INFO           0x4F00
#define VBE_FUNCTION_MODE_INFO      0x4F01
#define VBE_FUNCTION_SET_MODE       0x4F02
#define VBE_FUNCTION_GET_MODE       0x4F03
#define VBE_FUNCTION_SET_PALETTE    0x4F09

// VBE Mode Attributes
#define VBE_MODE_SUPPORTED          0x01
#define VBE_MODE_COLOR              0x08
#define VBE_MODE_GRAPHICS           0x10
#define VBE_MODE_LINEAR_FB          0x80


// COLOR DEFINITIONS


// VGA 16-Color Palette (Text Mode)
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


// RGB COLOR STRUCTURES


// 24-bit RGB Color
typedef struct {
    uint8_t r;  // Red (0-255)
    uint8_t g;  // Green (0-255)
    uint8_t b;  // Blue (0-255)
} rgb_color_t;

// 32-bit RGBA Color (with Alpha channel)
typedef struct {
    uint8_t r;     // Red (0-255)
    uint8_t g;     // Green (0-255)
    uint8_t b;     // Blue (0-255)
    uint8_t alpha; // Alpha/Transparency (0-255, 255=opaque)
} rgba_color_t;

// 16-bit RGB Color (5:6:5 format for high color modes)
typedef uint16_t rgb565_t;

// 15-bit RGB Color (5:5:5 format)
typedef uint16_t rgb555_t;


// VIDEO MODE STRUCTURES


// Video Mode Type
typedef enum {
    VGA_MODE_TEXT = 0,
    VGA_MODE_GRAPHICS = 1
} vga_mode_type_t;

// Video Mode Information
typedef struct {
    uint16_t mode_number;       // Mode number (e.g., 0x13, 0x101)
    vga_mode_type_t type;       // Text or Graphics
    uint16_t width;             // Width in pixels or columns
    uint16_t height;            // Height in pixels or rows
    uint8_t bpp;                // Bits per pixel (4, 8, 16, 24, 32)
    uint32_t framebuffer;       // Physical framebuffer address
    uint32_t framebuffer_size;  // Size of framebuffer in bytes
    uint16_t pitch;             // Bytes per scanline
    uint8_t is_linear;          // Linear framebuffer (1) or segmented (0)
    uint8_t is_vbe;             // VBE mode (1) or standard VGA (0)
} vga_mode_info_t;

// VBE Info Block (Version 2.0+)
typedef struct __attribute__((packed)) {
    uint8_t signature[4];       // "VESA"
    uint16_t version;           // VBE version (e.g., 0x0300 = 3.0)
    uint32_t oem_string;        // Pointer to OEM string
    uint32_t capabilities;      // Capabilities flags
    uint32_t video_modes;       // Pointer to video mode list
    uint16_t total_memory;      // Total video memory in 64KB blocks
    uint16_t oem_software_rev;  // OEM software revision
    uint32_t oem_vendor_name;   // Pointer to vendor name
    uint32_t oem_product_name;  // Pointer to product name
    uint32_t oem_product_rev;   // Pointer to product revision
    uint8_t reserved[222];      // Reserved
    uint8_t oem_data[256];      // OEM data
} vbe_info_block_t;

// VBE Mode Info Block
typedef struct __attribute__((packed)) {
    uint16_t attributes;        // Mode attributes
    uint8_t window_a;           // Window A attributes
    uint8_t window_b;           // Window B attributes
    uint16_t granularity;       // Window granularity
    uint16_t window_size;       // Window size
    uint16_t segment_a;         // Window A segment
    uint16_t segment_b;         // Window B segment
    uint32_t win_func_ptr;      // Window function pointer
    uint16_t pitch;             // Bytes per scan line
    uint16_t width;             // Width in pixels
    uint16_t height;            // Height in pixels
    uint8_t w_char;             // Character cell width
    uint8_t y_char;             // Character cell height
    uint8_t planes;             // Number of memory planes
    uint8_t bpp;                // Bits per pixel
    uint8_t banks;              // Number of banks
    uint8_t memory_model;       // Memory model type
    uint8_t bank_size;          // Bank size in KB
    uint8_t image_pages;        // Number of image pages
    uint8_t reserved0;          // Reserved
    uint8_t red_mask;           // Red mask size
    uint8_t red_position;       // Red field position
    uint8_t green_mask;         // Green mask size
    uint8_t green_position;     // Green field position
    uint8_t blue_mask;          // Blue mask size
    uint8_t blue_position;      // Blue field position
    uint8_t reserved_mask;      // Reserved mask size
    uint8_t reserved_position;  // Reserved field position
    uint8_t direct_color_attributes; // Direct color mode attributes
    uint32_t framebuffer;       // Physical address of framebuffer
    uint32_t off_screen_mem_off;// Pointer to off-screen memory
    uint16_t off_screen_mem_size;// Amount of off-screen memory
    uint8_t reserved1[206];     // Reserved
} vbe_mode_info_t;


// DRAWING & GRAPHICS ENUMERATIONS


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


// CORE INITIALIZATION & MODE SWITCHING


void vga_init(void);
int vga_set_mode(uint16_t mode);
int vga_get_current_mode(void);
vga_mode_info_t* vga_get_mode_info(void);
int vga_detect_vbe(void);
int vga_get_vbe_info(vbe_info_block_t* info);
int vga_get_vbe_mode_info(uint16_t mode, vbe_mode_info_t* info);
void vga_list_available_modes(void);


// TEXT MODE FUNCTIONS
// Basic Text I/O
void vga_putc(char c);
void vga_puts(const char *s);
void vga_clear(void);
void vga_clear_all(void);
void update_cursor(uint8_t row, uint8_t col);
void vga_erase_char(uint8_t row, uint8_t col);
void vga_set_color(uint8_t color_attribute);
uint8_t vga_get_row(void);
uint8_t vga_get_col(void);
void vga_backspace(void);
void vga_set_position(uint8_t row, uint8_t col);
void vga_scroll_up(void);
void vga_scroll_down(void);
void vga_scroll_up_view(void);
void vga_scroll_to_bottom(void);
uint16_t* vga_get_buffer(void);

// Advanced Text Drawing
void vga_draw_char(uint8_t row, uint8_t col, char c);
void vga_draw_char_color(uint8_t row, uint8_t col, char c, uint8_t color);
void vga_fill_rect(uint8_t row, uint8_t col, uint8_t width, uint8_t height, char c);
void vga_fill_rect_color(uint8_t row, uint8_t col, uint8_t width, uint8_t height, char c, uint8_t color);
void vga_draw_box(uint8_t row, uint8_t col, uint8_t width, uint8_t height, int double_line);
void vga_draw_box_color(uint8_t row, uint8_t col, uint8_t width, uint8_t height, int double_line, uint8_t color);
void vga_draw_hline(uint8_t row, uint8_t col, uint8_t width, char c);
void vga_draw_vline(uint8_t col, uint8_t row, uint8_t height, char c);
void vga_draw_hline_color(uint8_t row, uint8_t col, uint8_t width, char c, uint8_t color);
void vga_draw_vline_color(uint8_t col, uint8_t row, uint8_t height, char c, uint8_t color);

// Text Formatting
void vga_puts_at(uint8_t row, uint8_t col, const char *s);
void vga_puts_at_color(uint8_t row, uint8_t col, const char *s, uint8_t color);
void vga_puts_aligned(uint8_t row, vga_text_align_t align, const char *s);
void vga_puts_aligned_color(uint8_t row, vga_text_align_t align, const char *s, uint8_t color);
void vga_printf_at(uint8_t row, uint8_t col, const char *fmt, ...);
void vga_printf_color(const char *fmt, uint8_t color, ...);

// Cursor Management
void vga_set_cursor_style(vga_cursor_style_t style);
void vga_enable_cursor(void);
void vga_disable_cursor(void);
void vga_hide_cursor(void);

// Text Mode Color Management
void vga_invert_colors(void);
void vga_brighten_color(uint8_t color);
uint8_t vga_blend_colors(uint8_t fg, uint8_t bg, uint8_t alpha);
void vga_color_gradient(uint8_t start_color, uint8_t end_color, uint8_t steps);

// Text Mode Screen Effects
void vga_clear_line(uint8_t row);
void vga_clear_region(uint8_t row, uint8_t col, uint8_t width, uint8_t height);
void vga_fill_screen(char c, uint8_t color);
void vga_screen_wipe(uint8_t direction, uint8_t speed);
void vga_screen_fade_to_color(uint8_t color, uint8_t steps);

// Text Mode Utilities
void vga_frame_buffer(void);
void vga_refresh_display(void);
void vga_get_text_at(uint8_t row, uint8_t col, char *buf, uint8_t len);
void vga_get_color_at(uint8_t row, uint8_t col);
uint16_t vga_measure_text(const char *s);
void vga_draw_progress_bar(uint8_t row, uint8_t col, uint8_t width, uint8_t percent, uint8_t color);


// COLOR CONVERSION & HEX COLOR SUPPORT


// Convert hex color string to RGB (e.g., "#FF00FF" or "FF00FF")
rgb_color_t vga_hex_to_rgb(const char* hex);

// Convert RGB to various formats
uint8_t vga_rgb_to_vga_color(rgb_color_t rgb);        // Best-match to 16-color palette
uint8_t vga_rgb_to_256_palette(rgb_color_t rgb);      // Convert to 256-color palette index
rgb565_t vga_rgb_to_rgb565(rgb_color_t rgb);          // Convert to 16-bit high color
rgb555_t vga_rgb_to_rgb555(rgb_color_t rgb);          // Convert to 15-bit color
uint32_t vga_rgb_to_rgb888(rgb_color_t rgb);          // Convert to 24-bit true color

// Convert between formats
rgb_color_t vga_rgb565_to_rgb(rgb565_t color);
rgb_color_t vga_rgb555_to_rgb(rgb555_t color);
rgb_color_t vga_rgb888_to_rgb(uint32_t color);
rgb_color_t vga_vga_color_to_rgb(uint8_t vga_color);

// Alpha blending
rgb_color_t vga_blend_rgb(rgb_color_t fg, rgb_color_t bg, uint8_t alpha);
rgba_color_t vga_blend_rgba(rgba_color_t fg, rgba_color_t bg);


// GRAPHICS MODE FUNCTIONS (PIXEL-LEVEL DRAWING)


// Basic Pixel Operations
void vga_plot_pixel(uint16_t x, uint16_t y, uint32_t color);
uint32_t vga_get_pixel(uint16_t x, uint16_t y);
void vga_clear_screen(uint32_t color);

// Line Drawing (Bresenham's algorithm)
void vga_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color);
void vga_draw_line_thick(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color, uint8_t thickness);

// Rectangle Drawing
void vga_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);
void vga_fill_rect_gfx(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);
void vga_draw_rounded_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t radius, uint32_t color);

// Circle Drawing (Midpoint circle algorithm)
void vga_draw_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint32_t color);
void vga_fill_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint32_t color);
void vga_draw_ellipse(uint16_t cx, uint16_t cy, uint16_t rx, uint16_t ry, uint32_t color);
void vga_fill_ellipse(uint16_t cx, uint16_t cy, uint16_t rx, uint16_t ry, uint32_t color);

// Polygon Drawing
void vga_draw_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
void vga_fill_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
void vga_draw_polygon(uint16_t* points, uint16_t num_points, uint32_t color);
void vga_fill_polygon(uint16_t* points, uint16_t num_points, uint32_t color);

// Bitmap/Image Operations
void vga_draw_bitmap(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* bitmap);
void vga_draw_bitmap_alpha(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t* bitmap, uint8_t alpha);
void vga_blit(uint16_t src_x, uint16_t src_y, uint16_t dst_x, uint16_t dst_y, uint16_t width, uint16_t height);
void vga_blit_scaled(uint16_t src_x, uint16_t src_y, uint16_t src_w, uint16_t src_h, uint16_t dst_x, uint16_t dst_y, uint16_t dst_w, uint16_t dst_h);

// Sprite Operations
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t* data;
} vga_sprite_t;

void vga_draw_sprite(uint16_t x, uint16_t y, const vga_sprite_t* sprite);
void vga_draw_sprite_transparent(uint16_t x, uint16_t y, const vga_sprite_t* sprite, uint32_t transparent_color);


// ADVANCED GRAPHICS FEATURES


// Palette Management (256-color modes)
void vga_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void vga_get_palette_entry(uint8_t index, uint8_t* r, uint8_t* g, uint8_t* b);
void vga_set_palette(const uint8_t* palette, uint16_t count);
void vga_fade_palette_to_black(uint8_t steps);
void vga_fade_palette_to_white(uint8_t steps);
void vga_rotate_palette(uint8_t start, uint8_t end);

// Double/Triple Buffering & V-Sync
void vga_enable_double_buffer(void);
void vga_disable_double_buffer(void);
void vga_swap_buffers(void);
void vga_wait_vsync(void);
void vga_enable_page_flipping(void);
void vga_flip_page(void);

// Framebuffer Access
void* vga_get_framebuffer(void);
uint32_t vga_get_framebuffer_size(void);
uint16_t vga_get_pitch(void);
void vga_copy_to_framebuffer(const void* data, uint32_t size);

// Advanced Effects
void vga_apply_filter_grayscale(void);
void vga_apply_filter_sepia(void);
void vga_apply_filter_invert(void);
void vga_apply_filter_blur(uint8_t radius);
void vga_apply_gamma_correction(float gamma);

#endif
