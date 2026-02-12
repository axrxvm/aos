/*
 * === AOS HEADER BEGIN ===
 * src/dev/keyboard.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <stdint.h>
#include <keyboard.h>
#include <io.h> //for inb and outb
#include <dev/mouse.h> // For mouse_handle_interrupt

#define KEYBOARD_PORT 0x60

// Keyboard state flags
static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;
static uint8_t alt_pressed = 0;
static uint8_t caps_lock = 0;
static uint8_t extended_scancode = 0;  // Flag for extended scancodes (0xE0 prefix)

// Scancode definitions
#define SCANCODE_LSHIFT_PRESSED  0x2A
#define SCANCODE_LSHIFT_RELEASED 0xAA
#define SCANCODE_RSHIFT_PRESSED  0x36
#define SCANCODE_RSHIFT_RELEASED 0xB6
#define SCANCODE_CTRL_PRESSED    0x1D
#define SCANCODE_CTRL_RELEASED   0x9D
#define SCANCODE_ALT_PRESSED     0x38
#define SCANCODE_ALT_RELEASED    0xB8
#define SCANCODE_CAPS_LOCK       0x3A
#define SCANCODE_EXTENDED        0xE0

// Extended (arrow) key scancodes
#define SCANCODE_UP              0x48
#define SCANCODE_DOWN            0x50
#define SCANCODE_LEFT            0x4B
#define SCANCODE_RIGHT           0x4D

// Getter functions to expose modifier states
uint8_t keyboard_is_ctrl_pressed(void) {
    return ctrl_pressed;
}

uint8_t keyboard_is_shift_pressed(void) {
    return shift_pressed;
}

uint8_t keyboard_is_alt_pressed(void) {
    return alt_pressed;
}

void keyboard_init(void) {
    // Basic PS/2 initialization (enable scanning)
    outb(0x64, 0xAE); // Enable keyboard
    
    // Initialize keyboard state
    shift_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;
    
    // Flush keyboard buffer
    keyboard_flush_buffer();
}

void keyboard_flush_buffer(void) {
    // Read and discard any pending data from PS/2 controller
    for (int i = 0; i < 16; i++) {
        uint8_t status = inb(0x64);
        if (status & 0x01) {
            inb(KEYBOARD_PORT); // Discard the data
        } else {
            break;
        }
    }
}

uint8_t keyboard_get_scancode(void) {
    uint8_t status = inb(0x64);
    
    // Check if data is available (bit 0)
    if (status & 0x01) {
        uint8_t data = inb(KEYBOARD_PORT);
        
        // Bit 5 = 1 means data is from auxiliary device (mouse)
        if (status & 0x20) {
            // Forward mouse data to mouse driver instead of discarding
            mouse_handle_interrupt(data);
            return 0;  // Not keyboard data
        }
        return data;  // Keyboard data
    }
    return 0;  // No data available
}

char scancode_to_char(uint8_t scancode) {
    // Handle extended scancode prefix (0xE0)
    if (scancode == SCANCODE_EXTENDED) {
        extended_scancode = 1;
        return 0;
    }
    
    // If we have extended prefix, return special key codes
    if (extended_scancode) {
        extended_scancode = 0;  // Reset flag
        
        // Arrow keys
        if (scancode == SCANCODE_UP) return 0x1E;      // KEY_UP
        if (scancode == SCANCODE_DOWN) return 0x1F;    // KEY_DOWN
        if (scancode == SCANCODE_LEFT) return 0x1A;    // KEY_LEFT
        if (scancode == SCANCODE_RIGHT) return 0x1B;   // KEY_RIGHT
        
        // Ignore other extended keys
        if (scancode & 0x80) {
            return 0;  // Key release
        }
        return 0;
    }
    
    // Handle modifier key state changes
    if (scancode == SCANCODE_LSHIFT_PRESSED || scancode == SCANCODE_RSHIFT_PRESSED) {
        shift_pressed = 1;
        return 0;
    }
    if (scancode == SCANCODE_LSHIFT_RELEASED || scancode == SCANCODE_RSHIFT_RELEASED) {
        shift_pressed = 0;
        return 0;
    }
    if (scancode == SCANCODE_CTRL_PRESSED) {
        ctrl_pressed = 1;
        return 0;
    }
    if (scancode == SCANCODE_CTRL_RELEASED) {
        ctrl_pressed = 0;
        return 0;
    }
    if (scancode == SCANCODE_ALT_PRESSED) {
        alt_pressed = 1;
        return 0;
    }
    if (scancode == SCANCODE_ALT_RELEASED) {
        alt_pressed = 0;
        return 0;
    }
    if (scancode == SCANCODE_CAPS_LOCK) {
        caps_lock = !caps_lock;
        return 0;
    }
    
    // Ignore key release events (high bit set)
    if (scancode & 0x80) {
        return 0;
    }
    
    // Handle special keys
    if (scancode == 0x1C) return '\n';  // Enter
    if (scancode == 0x0E) return '\b';  // Backspace
    if (scancode == 0x0F) return '\t';  // Tab
    if (scancode == 0x39) return ' ';   // Space
    if (scancode == 0x01) return 0x1B;  // Escape
    
    // US QWERTY mapping - lowercase/unshifted
    static const char scancode_map_lower[] = {
        0,    0,   '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',  '-',  '=',  0,    0,    // 0x00-0x0F
        'q',  'w', 'e',  'r',  't',  'y',  'u',  'i',  'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',  // 0x10-0x1F
        'd',  'f', 'g',  'h',  'j',  'k',  'l',  ';',  '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  // 0x20-0x2F
        'b',  'n', 'm',  ',',  '.',  '/',  0,    '*',  0,    ' ',  0,    0,    0,    0,    0,    0,    // 0x30-0x3F
        0,    0,   0,    0,    0,    0,    0,    '7',  '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',  // 0x40-0x4F
        '2',  '3', '0',  '.',  0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0     // 0x50-0x5F
    };
    
    // US QWERTY mapping - uppercase/shifted
    static const char scancode_map_upper[] = {
        0,    0,   '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',  '_',  '+',  0,    0,    // 0x00-0x0F
        'Q',  'W', 'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',  // 0x10-0x1F
        'D',  'F', 'G',  'H',  'J',  'K',  'L',  ':',  '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',  // 0x20-0x2F
        'B',  'N', 'M',  '<',  '>',  '?',  0,    '*',  0,    ' ',  0,    0,    0,    0,    0,    0,    // 0x30-0x3F
        0,    0,   0,    0,    0,    0,    0,    '7',  '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',  // 0x40-0x4F
        '2',  '3', '0',  '.',  0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0     // 0x50-0x5F
    };
    
    // Bounds check
    if (scancode >= sizeof(scancode_map_lower)) {
        return 0;
    }
    
    char c;
    
    // Select character based on shift state
    if (shift_pressed) {
        c = scancode_map_upper[scancode];
    } else {
        c = scancode_map_lower[scancode];
    }
    
    // Apply caps lock to letters only
    if (caps_lock && c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    } else if (caps_lock && c >= 'A' && c <= 'Z' && !shift_pressed) {
        // If caps lock is on but shift is not pressed, we already have uppercase
        // from the lower map, so do nothing
    } else if (caps_lock && c >= 'A' && c <= 'Z' && shift_pressed) {
        // If caps lock is on AND shift is pressed, invert to lowercase
        c = c - 'A' + 'a';
    }
    
    return c;
}

// Inline assembly for I/O (now declared in io.h)
// static inline void outb(uint16_t port, uint8_t data) {
//     __asm__ volatile ("outb %0, %1" : : "a" (data), "dN" (port));
// }

// static inline uint8_t inb(uint16_t port) {
//     uint8_t data;
//     __asm__ volatile ("inb %1, %0" : "=a" (data) : "dN" (port));
//     return data;
// }
