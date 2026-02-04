/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/editor.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


// src/kernel/editor.c
#include <editor.h>
#include <vga.h>
#include <keyboard.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <serial.h>

// Helper: Clear the entire screen
static void editor_clear_screen(void) {
    vga_set_color(0x0F);  // White on black
    vga_clear();
}

// Helper: Print character at position
static void editor_putc_at(uint32_t row, uint32_t col, char c) {
    if (row < 25 && col < 80) {
        uint16_t* buffer = vga_get_buffer();
        uint32_t pos = row * 80 + col;
        buffer[pos] = ((0x0F) << 8) | c;
    }
}

// Initialize editor context
void editor_init(editor_context_t* ctx) {
    memset(ctx, 0, sizeof(editor_context_t));
    ctx->num_lines = 1;
    ctx->max_lines = EDITOR_MAX_LINES;
    ctx->cursor_line = 0;
    ctx->cursor_col = 0;
    ctx->view_line = 0;
    ctx->view_col = 0;
    ctx->mode = EDITOR_EDIT;
    ctx->modified = 0;
    
    // Initialize first empty line
    memset(ctx->lines[0].data, 0, EDITOR_MAX_LINE_LENGTH);
    ctx->lines[0].length = 0;
}

// Create new file
void editor_new_file(editor_context_t* ctx, const char* filename) {
    editor_init(ctx);
    if (filename) {
        strncpy(ctx->filename, filename, sizeof(ctx->filename) - 1);
        ctx->filename[sizeof(ctx->filename) - 1] = '\0';
    }
}

// Open file into editor
int editor_open_file(editor_context_t* ctx, const char* filename) {
    editor_init(ctx);
    strncpy(ctx->filename, filename, sizeof(ctx->filename) - 1);
    ctx->filename[sizeof(ctx->filename) - 1] = '\0';
    
    // Open file for reading
    int fd = sys_open(filename, O_RDONLY);
    if (fd < 0) {
        serial_puts("editor_open_file: Cannot open file\n");
        return -1;
    }
    
    // Read file into buffer
    uint32_t line_idx = 0;
    uint32_t col_idx = 0;
    char buf[512];
    int bytes_read;
    
    while ((bytes_read = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < bytes_read && line_idx < EDITOR_MAX_LINES; i++) {
            char c = buf[i];
            
            if (c == '\n') {
                // End of line
                ctx->lines[line_idx].data[col_idx] = '\0';
                ctx->lines[line_idx].length = col_idx;
                line_idx++;
                col_idx = 0;
                
                if (line_idx >= EDITOR_MAX_LINES) {
                    break;
                }
            } else if (c != '\r') {  // Ignore carriage returns
                // Add character to current line
                if (col_idx < EDITOR_MAX_LINE_LENGTH - 1) {
                    ctx->lines[line_idx].data[col_idx++] = c;
                }
            }
        }
    }
    
    sys_close(fd);
    
    // Finalize last line
    if (col_idx > 0 || line_idx == 0) {
        ctx->lines[line_idx].data[col_idx] = '\0';
        ctx->lines[line_idx].length = col_idx;
        line_idx++;
    }
    
    ctx->num_lines = (line_idx > 0) ? line_idx : 1;
    ctx->cursor_line = 0;
    ctx->cursor_col = 0;
    ctx->view_line = 0;
    ctx->view_col = 0;
    ctx->modified = 0;
    
    return 0;
}

// Save file
int editor_save_file(editor_context_t* ctx) {
    if (ctx->filename[0] == '\0') {
        return -1;  // No filename
    }
    
    // Open file for writing (truncate)
    int fd = sys_open(ctx->filename, O_CREAT | O_WRONLY | O_TRUNC);
    if (fd < 0) {
        serial_puts("editor_save_file: Cannot open file for writing\n");
        return -1;
    }
    
    // Write all lines
    for (uint32_t i = 0; i < ctx->num_lines; i++) {
        // Write line content
        if (ctx->lines[i].length > 0) {
            sys_write(fd, ctx->lines[i].data, ctx->lines[i].length);
        }
        
        // Write newline (except possibly for last line)
        if (i < ctx->num_lines - 1 || ctx->lines[i].length > 0) {
            sys_write(fd, "\n", 1);
        }
    }
    
    sys_close(fd);
    ctx->modified = 0;
    
    return 0;
}

// Save file with new name
int editor_save_as_file(editor_context_t* ctx, const char* filename) {
    strncpy(ctx->filename, filename, sizeof(ctx->filename) - 1);
    ctx->filename[sizeof(ctx->filename) - 1] = '\0';
    return editor_save_file(ctx);
}

// Insert character at cursor position
void editor_insert_char(editor_context_t* ctx, char c) {
    if (ctx->cursor_line >= ctx->num_lines) {
        return;
    }
    
    editor_line_t* line = &ctx->lines[ctx->cursor_line];
    
    // Bounds check
    if (ctx->cursor_col > line->length) {
        ctx->cursor_col = line->length;
    }
    
    if (line->length >= EDITOR_MAX_LINE_LENGTH - 1) {
        return;  // Line too long
    }
    
    // Shift characters to the right
    for (uint32_t i = line->length; i > ctx->cursor_col; i--) {
        line->data[i] = line->data[i - 1];
    }
    
    // Insert character
    line->data[ctx->cursor_col] = c;
    line->length++;
    line->data[line->length] = '\0';
    
    // Move cursor
    ctx->cursor_col++;
    ctx->modified = 1;
    
    // Scroll right if needed
    if (ctx->cursor_col - ctx->view_col >= EDITOR_DISPLAY_WIDTH) {
        ctx->view_col = ctx->cursor_col - EDITOR_DISPLAY_WIDTH + 1;
    }
}

// Delete character at cursor (backspace behavior)
void editor_delete_char(editor_context_t* ctx) {
    if (ctx->cursor_line >= ctx->num_lines) {
        return;
    }
    
    editor_line_t* line = &ctx->lines[ctx->cursor_line];
    
    if (ctx->cursor_col == 0) {
        // At beginning of line - merge with previous line
        if (ctx->cursor_line == 0) {
            return;  // First line, nowhere to merge
        }
        
        editor_line_t* prev_line = &ctx->lines[ctx->cursor_line - 1];
        
        // Check if merge would overflow
        if (prev_line->length + line->length >= EDITOR_MAX_LINE_LENGTH) {
            return;  // Can't merge
        }
        
        // Append current line to previous line
        memcpy(prev_line->data + prev_line->length, line->data, line->length);
        prev_line->length += line->length;
        prev_line->data[prev_line->length] = '\0';
        
        // Delete current line (shift all following lines up)
        for (uint32_t i = ctx->cursor_line; i < ctx->num_lines - 1; i++) {
            ctx->lines[i] = ctx->lines[i + 1];
        }
        ctx->num_lines--;
        
        // Move cursor to end of previous line
        ctx->cursor_line--;
        ctx->cursor_col = prev_line->length;
        ctx->modified = 1;
        
    } else {
        // Delete character before cursor
        ctx->cursor_col--;
        
        // Shift characters to the left
        for (uint32_t i = ctx->cursor_col; i < line->length; i++) {
            line->data[i] = line->data[i + 1];
        }
        
        line->length--;
        line->data[line->length] = '\0';
        ctx->modified = 1;
    }
}

// Insert newline (split line at cursor)
void editor_new_line(editor_context_t* ctx) {
    if (ctx->cursor_line >= ctx->num_lines) {
        return;
    }
    
    if (ctx->num_lines >= EDITOR_MAX_LINES) {
        return;  // Too many lines
    }
    
    editor_line_t* line = &ctx->lines[ctx->cursor_line];
    uint32_t split_at = ctx->cursor_col;
    
    // Shift all following lines down
    for (uint32_t i = ctx->num_lines; i > ctx->cursor_line; i--) {
        ctx->lines[i] = ctx->lines[i - 1];
    }
    ctx->num_lines++;
    
    // Split current line
    uint32_t second_len = line->length - split_at;
    char* second_data = ctx->lines[ctx->cursor_line + 1].data;
    
    memcpy(second_data, line->data + split_at, second_len);
    second_data[second_len] = '\0';
    ctx->lines[ctx->cursor_line + 1].length = second_len;
    
    // Truncate first line
    line->data[split_at] = '\0';
    line->length = split_at;
    
    // Move cursor to start of next line
    ctx->cursor_line++;
    ctx->cursor_col = 0;
    ctx->view_col = 0;
    ctx->modified = 1;
}

// Move cursor
void editor_move_cursor(editor_context_t* ctx, int dx, int dy) {
    // Ensure ctx is valid and has at least one line
    if (!ctx || ctx->num_lines == 0) {
        return;
    }
    
    // Move vertical first
    if (dy < 0 && ctx->cursor_line > 0) {
        ctx->cursor_line--;
    } else if (dy > 0 && ctx->cursor_line < ctx->num_lines - 1) {
        ctx->cursor_line++;
    }
    
    // Ensure cursor_line is within bounds
    if (ctx->cursor_line >= ctx->num_lines) {
        ctx->cursor_line = ctx->num_lines - 1;
    }
    
    // Get current line and ensure cursor_col is within bounds
    editor_line_t* line = &ctx->lines[ctx->cursor_line];
    if (ctx->cursor_col > line->length) {
        ctx->cursor_col = line->length;
    }
    
    // Move horizontal
    if (dx < 0 && ctx->cursor_col > 0) {
        ctx->cursor_col--;
    } else if (dx > 0) {
        // Allow cursor to move one past the end of the line (for insertion)
        if (ctx->cursor_col < line->length) {
            ctx->cursor_col++;
        }
    }
    
    // Final bounds check
    if (ctx->cursor_col > line->length) {
        ctx->cursor_col = line->length;
    }
    
    // Scroll viewport to keep cursor visible
    editor_scroll_to_cursor(ctx);
}

// Scroll viewport to keep cursor visible
void editor_scroll_to_cursor(editor_context_t* ctx) {
    if (!ctx || ctx->num_lines == 0) {
        return;
    }
    
    // Ensure cursor is within valid bounds first
    if (ctx->cursor_line >= ctx->num_lines) {
        ctx->cursor_line = ctx->num_lines - 1;
    }
    
    // Vertical scrolling - keep cursor visible
    if (ctx->cursor_line < ctx->view_line) {
        ctx->view_line = ctx->cursor_line;
    } else if (ctx->cursor_line >= ctx->view_line + EDITOR_DISPLAY_HEIGHT) {
        ctx->view_line = ctx->cursor_line - EDITOR_DISPLAY_HEIGHT + 1;
    }
    
    // Ensure view_line doesn't go negative or too far
    if (ctx->view_line > ctx->num_lines) {
        ctx->view_line = 0;
    }
    
    // Horizontal scrolling - keep cursor visible
    if (ctx->cursor_col < ctx->view_col) {
        ctx->view_col = ctx->cursor_col;
    } else if (ctx->cursor_col >= ctx->view_col + EDITOR_DISPLAY_WIDTH) {
        ctx->view_col = ctx->cursor_col - EDITOR_DISPLAY_WIDTH + 1;
    }
    
    // Ensure view_col doesn't go negative
    if (ctx->view_col > EDITOR_MAX_LINE_LENGTH) {
        ctx->view_col = 0;
    }
}

// Display the editor
void editor_display(editor_context_t* ctx) {
    editor_clear_screen();
    
    // Calculate cursor screen position for visual indicator
    int32_t cursor_screen_row = -1;
    int32_t cursor_screen_col = -1;
    if (ctx->cursor_line >= ctx->view_line && ctx->cursor_line < ctx->view_line + EDITOR_DISPLAY_HEIGHT) {
        cursor_screen_row = ctx->cursor_line - ctx->view_line;
        if (ctx->cursor_col >= ctx->view_col && ctx->cursor_col < ctx->view_col + EDITOR_DISPLAY_WIDTH) {
            cursor_screen_col = ctx->cursor_col - ctx->view_col;
        }
    }
    
    // Draw file content
    uint32_t line_count = 0;
    for (uint32_t i = ctx->view_line; i < ctx->num_lines && line_count < EDITOR_DISPLAY_HEIGHT; i++) {
        editor_line_t* line = &ctx->lines[i];
        
        // Draw line content (with scrolling)
        uint32_t col = 0;
        for (uint32_t j = ctx->view_col; j < line->length && col < EDITOR_DISPLAY_WIDTH; j++) {
            char c = line->data[j];
            
            // Check if this is the cursor position - if so, draw with inverted colors
            if (line_count == cursor_screen_row && col == cursor_screen_col) {
                uint16_t* buffer = vga_get_buffer();
                uint32_t pos = line_count * 80 + col;
                // Inverted colors: 0x70 = white background, black foreground
                buffer[pos] = ((0x70) << 8) | c;
            } else {
                editor_putc_at(line_count, col, c);
            }
            col++;
        }
        
        // Draw cursor if it's at the end of this line (after last character)
        if (line_count == cursor_screen_row && col == cursor_screen_col) {
            uint16_t* buffer = vga_get_buffer();
            uint32_t pos = line_count * 80 + col;
            // Inverted space character to show cursor position
            buffer[pos] = ((0x70) << 8) | ' ';
        }
        
        // Clear rest of line
        while (col < EDITOR_DISPLAY_WIDTH) {
            editor_putc_at(line_count, col, ' ');
            col++;
        }
        
        line_count++;
    }
    
    // Clear remaining display lines
    while (line_count < EDITOR_DISPLAY_HEIGHT) {
        for (uint32_t col = 0; col < EDITOR_DISPLAY_WIDTH; col++) {
            editor_putc_at(line_count, col, ' ');
        }
        line_count++;
    }
    
    // Draw status bar
    editor_display_status_bar(ctx);
    
    // Update hardware cursor position based on visible cursor location
    // Ensure cursor is always within valid screen bounds
    if (cursor_screen_row >= 0 && cursor_screen_row < EDITOR_DISPLAY_HEIGHT) {
        uint8_t screen_row = (uint8_t)cursor_screen_row;
        uint8_t screen_col = (cursor_screen_col >= 0 && cursor_screen_col < EDITOR_DISPLAY_WIDTH) ? 
                             (uint8_t)cursor_screen_col : 0;
        update_cursor(screen_row, screen_col);
    } else {
        // Cursor is off-screen, hide it temporarily
        // Position it at a safe location (bottom right of edit area)
        update_cursor(EDITOR_DISPLAY_HEIGHT - 1, EDITOR_DISPLAY_WIDTH - 1);
    }
}

// Display status bar
void editor_display_status_bar(editor_context_t* ctx) {
    // Bottom line - status bar
    uint32_t status_row = 23;
    uint16_t* buffer = vga_get_buffer();
    
    char status[80];
    char line_str[16], col_str[16];
    
    itoa(ctx->cursor_line + 1, line_str, 10);
    itoa(ctx->cursor_col + 1, col_str, 10);
    
    strcpy(status, "Line ");
    strcat(status, line_str);
    strcat(status, ", Col ");
    strcat(status, col_str);
    strcat(status, " | ");
    strcat(status, ctx->filename);
    if (ctx->modified) {
        strcat(status, " [MODIFIED]");
    }
    
    // Clear status line
    for (uint32_t col = 0; col < 80; col++) {
        buffer[status_row * 80 + col] = ((0x70) << 8) | ' ';  // Inverse colors
    }
    
    // Print status
    uint32_t pos = status_row * 80;
    for (uint32_t i = 0; i < 80 && status[i]; i++) {
        buffer[pos + i] = ((0x70) << 8) | status[i];
    }
    
    // Help line
    uint32_t help_row = 24;
    const char* help = "Ctrl+S: Save | Ctrl+X: Exit | Ctrl+H: Help";
    
    // Clear help line
    for (uint32_t col = 0; col < 80; col++) {
        buffer[help_row * 80 + col] = ((0x0F) << 8) | ' ';
    }
    
    // Print help
    pos = help_row * 80;
    for (uint32_t i = 0; i < 80 && help[i]; i++) {
        buffer[pos + i] = ((0x0F) << 8) | help[i];
    }
}

// Handle input
void editor_handle_input(editor_context_t* ctx) {
    uint8_t scancode = keyboard_get_scancode();
    if (scancode == 0) {
        return;
    }
    
    char c = scancode_to_char(scancode);
    
    // Arrow key handling
    if (c == KEY_UP) {
        editor_move_cursor(ctx, 0, -1);
        return;
    }
    if (c == KEY_DOWN) {
        editor_move_cursor(ctx, 0, 1);
        return;
    }
    if (c == KEY_LEFT) {
        editor_move_cursor(ctx, -1, 0);
        return;
    }
    if (c == KEY_RIGHT) {
        editor_move_cursor(ctx, 1, 0);
        return;
    }
    
    // Special keys
    if (c == 0x1B) {
        // ESC - show command line
        return;
    }
    
    // Ctrl+S - Save (check for Ctrl modifier)
    if (c == 's' && keyboard_is_ctrl_pressed()) {  // Ctrl+S
        if (editor_save_file(ctx) == 0) {
            serial_puts("File saved\n");
        }
        return;
    }
    
    // Ctrl+X - Exit
    if (c == 'x' && keyboard_is_ctrl_pressed()) {  // Ctrl+X
        // Will be handled by main editor loop
        ctx->mode = EDITOR_NORMAL;
        return;
    }
    
    // Regular character input
    if (c >= 32 && c <= 126) {
        editor_insert_char(ctx, c);
    } else if (c == '\n') {
        editor_new_line(ctx);
    } else if (c == '\b') {
        editor_delete_char(ctx);
    } else if (c == '\t') {
        // Tab - insert 4 spaces
        for (int i = 0; i < 4; i++) {
            editor_insert_char(ctx, ' ');
        }
    }
}

// Main editor loop
void editor_run(editor_context_t* ctx) {
    editor_clear_screen();
    ctx->mode = EDITOR_EDIT;
    int needs_redraw = 1;  // Force initial redraw
    
    // Initialize cursor for editor with blinking underline style for better visibility
    vga_set_cursor_style(CURSOR_BLINK);
    vga_enable_cursor();
    
    // Ensure cursor is at a valid position
    if (ctx->cursor_line >= ctx->num_lines) {
        ctx->cursor_line = (ctx->num_lines > 0) ? (ctx->num_lines - 1) : 0;
    }
    if (ctx->cursor_col > ctx->lines[ctx->cursor_line].length) {
        ctx->cursor_col = ctx->lines[ctx->cursor_line].length;
    }
    
    // Ensure viewport shows the cursor
    editor_scroll_to_cursor(ctx);
    
    serial_puts("Editor started. Use Ctrl+S to save, Ctrl+X to exit\n");
    serial_puts("Cursor: Use arrow keys to move, characters insert at cursor position\n");
    
    while (ctx->mode == EDITOR_EDIT) {
        // Only redraw if content changed
        if (needs_redraw) {
            editor_display(ctx);
            needs_redraw = 0;
        }
        
        // Store cursor position to detect changes
        uint32_t old_line = ctx->cursor_line;
        uint32_t old_col = ctx->cursor_col;
        int old_modified = ctx->modified;
        
        editor_handle_input(ctx);
        
        // Redraw if cursor moved or content changed
        if (ctx->cursor_line != old_line || ctx->cursor_col != old_col || ctx->modified != old_modified) {
            needs_redraw = 1;
        }
    }
    
    if (ctx->modified) {
        vga_clear_all();
        vga_puts("Save file before exiting? (y/n): ");
        
        // Simple yes/no prompt
        while (1) {
            uint8_t scancode = keyboard_get_scancode();
            if (scancode == 0) continue;
            
            char c = scancode_to_char(scancode);
            if (c == 'y' || c == 'Y') {
                vga_putc('y');
                vga_putc('\n');
                editor_save_file(ctx);
                break;
            } else if (c == 'n' || c == 'N') {
                vga_putc('n');
                vga_putc('\n');
                break;
            }
        }
    }
    
    editor_clear_screen();
}

// Cleanup
void editor_cleanup(editor_context_t* ctx) {
    memset(ctx, 0, sizeof(editor_context_t));
}
