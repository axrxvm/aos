/*
 * === AOS HEADER BEGIN ===
 * ./include/editor.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef EDITOR_H
#define EDITOR_H

#include <stdint.h>
#include <stddef.h>

// Editor configuration
#define EDITOR_MAX_LINES 1000
#define EDITOR_MAX_LINE_LENGTH 256
#define EDITOR_DISPLAY_HEIGHT 22      // Leave 2 lines for status bar and input
#define EDITOR_DISPLAY_WIDTH 80

// Editor states
typedef enum {
    EDITOR_EDIT,
    EDITOR_NORMAL,
    EDITOR_COMMAND
} editor_mode_t;

// Text buffer line structure
typedef struct {
    char data[EDITOR_MAX_LINE_LENGTH];
    uint32_t length;
} editor_line_t;

// Editor context
typedef struct {
    editor_line_t lines[EDITOR_MAX_LINES];
    uint32_t num_lines;
    uint32_t max_lines;
    
    // Cursor position
    uint32_t cursor_line;
    uint32_t cursor_col;
    
    // Viewport
    uint32_t view_line;      // First line visible on screen
    uint32_t view_col;       // First column visible on screen
    
    // File info
    char filename[256];
    int modified;            // 1 if modified, 0 otherwise
    
    // Edit mode
    editor_mode_t mode;
    
} editor_context_t;

// Function declarations
void editor_init(editor_context_t* ctx);
void editor_new_file(editor_context_t* ctx, const char* filename);
int editor_open_file(editor_context_t* ctx, const char* filename);
int editor_save_file(editor_context_t* ctx);
int editor_save_as_file(editor_context_t* ctx, const char* filename);
void editor_run(editor_context_t* ctx);
void editor_cleanup(editor_context_t* ctx);

// Internal functions
void editor_display(editor_context_t* ctx);
void editor_display_status_bar(editor_context_t* ctx);
void editor_handle_input(editor_context_t* ctx);
void editor_insert_char(editor_context_t* ctx, char c);
void editor_delete_char(editor_context_t* ctx);
void editor_delete_line(editor_context_t* ctx);
void editor_new_line(editor_context_t* ctx);
void editor_move_cursor(editor_context_t* ctx, int dx, int dy);
void editor_scroll_to_cursor(editor_context_t* ctx);

#endif // EDITOR_H
