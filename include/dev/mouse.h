/*
 * === AOS HEADER BEGIN ===
 * ./include/dev/mouse.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */

#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

// Mouse event types
#define MOUSE_EVENT_MOVE        0x01
#define MOUSE_EVENT_LEFT_BTN    0x02
#define MOUSE_EVENT_RIGHT_BTN   0x04
#define MOUSE_EVENT_MIDDLE_BTN  0x08
#define MOUSE_EVENT_SCROLL_UP   0x10
#define MOUSE_EVENT_SCROLL_DOWN 0x20

// Mouse packet structure
typedef struct {
    uint8_t buttons;      // Button states (bit 0=left, 1=right, 2=middle)
    int8_t x_movement;    // X-axis movement
    int8_t y_movement;    // Y-axis movement
    int8_t z_movement;    // Scroll wheel movement (if available)
} mouse_packet_t;

/**
 * Initialize PS/2 mouse
 */
void mouse_init(void);

/**
 * Get the latest mouse packet
 * @return Pointer to the latest mouse packet, or NULL if no new data
 */
mouse_packet_t* mouse_get_packet(void);

/**
 * Check if mouse has new data available
 * @return 1 if new data is available, 0 otherwise
 */
uint8_t mouse_has_data(void);

/**
 * Get mouse event flags from the latest packet
 * @return Event flags (MOUSE_EVENT_*)
 */
uint8_t mouse_get_events(void);

/**
 * Poll the mouse for new data (use if IRQ12 is not set up)
 */
void mouse_poll(void);

/**
 * Handle mouse interrupt (should be called from IRQ12 handler)
 * @param mouse_byte Byte received from the mouse
 */
void mouse_handle_interrupt(uint8_t mouse_byte);

#endif
