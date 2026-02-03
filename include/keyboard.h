/*
 * === AOS HEADER BEGIN ===
 * ./include/keyboard.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Special key codes returned by scancode_to_char for arrow keys
#define KEY_UP      0x1E
#define KEY_DOWN    0x1F
#define KEY_LEFT    0x1A  // Use a different code to avoid conflicts
#define KEY_RIGHT   0x1B  // Use a different code to avoid conflicts

void keyboard_init(void);
void keyboard_flush_buffer(void);
uint8_t keyboard_get_scancode(void);
char scancode_to_char(uint8_t scancode);
uint8_t keyboard_is_ctrl_pressed(void);
uint8_t keyboard_is_shift_pressed(void);
uint8_t keyboard_is_alt_pressed(void);

#endif
