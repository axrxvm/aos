/*
 * === AOS HEADER BEGIN ===
 * src/dev/mouse.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <dev/mouse.h>
#include <io.h>
#include <stdint.h>

// PS/2 Mouse ports
#define MOUSE_DATA_PORT     0x60
#define MOUSE_COMMAND_PORT  0x64

// Mouse commands
#define MOUSE_CMD_ENABLE_PACKET_STREAMING   0xF4
#define MOUSE_CMD_SET_SAMPLE_RATE          0xF3
#define MOUSE_CMD_GET_DEVICE_ID            0xF2
#define MOUSE_CMD_SET_DEFAULTS             0xF6

// Controller commands
#define CONTROLLER_CMD_READ_CONFIG          0x20
#define CONTROLLER_CMD_WRITE_CONFIG         0x60
#define CONTROLLER_CMD_ENABLE_AUX           0xA8
#define CONTROLLER_CMD_WRITE_TO_MOUSE       0xD4

// Mouse state
static mouse_packet_t current_packet = {0};
static mouse_packet_t previous_packet = {0};
static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[4] = {0};
static uint8_t has_scroll_wheel = 0;
static uint8_t new_data_available = 0;

/**
 * Wait for the mouse controller to be ready to receive commands
 */
static void mouse_wait_input(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(MOUSE_COMMAND_PORT) & 0x02) == 0) {
            return;
        }
    }
}

/**
 * Wait for the mouse controller to have data available
 */
static void mouse_wait_output(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(MOUSE_COMMAND_PORT) & 0x01) {
            return;
        }
    }
}

/**
 * Write a byte to the mouse
 */
static void mouse_write(uint8_t data) {
    mouse_wait_input();
    outb(MOUSE_COMMAND_PORT, CONTROLLER_CMD_WRITE_TO_MOUSE);
    mouse_wait_input();
    outb(MOUSE_DATA_PORT, data);
}

/**
 * Read a byte from the mouse
 */
static uint8_t mouse_read(void) {
    mouse_wait_output();
    return inb(MOUSE_DATA_PORT);
}

/**
 * Try to enable scroll wheel support (IntelliMouse protocol)
 */
static void mouse_try_enable_scrollwheel(void) {
    // Try to enable scroll wheel by setting special sample rates
    // This activates IntelliMouse mode with 4-byte packets
    
    // Set sample rate to 200
    mouse_write(MOUSE_CMD_SET_SAMPLE_RATE);
    mouse_read(); // ACK
    mouse_write(200);
    mouse_read(); // ACK
    
    // Set sample rate to 100
    mouse_write(MOUSE_CMD_SET_SAMPLE_RATE);
    mouse_read(); // ACK
    mouse_write(100);
    mouse_read(); // ACK
    
    // Set sample rate to 80
    mouse_write(MOUSE_CMD_SET_SAMPLE_RATE);
    mouse_read(); // ACK
    mouse_write(80);
    mouse_read(); // ACK
    
    // Get device ID to check if scroll wheel is enabled
    mouse_write(MOUSE_CMD_GET_DEVICE_ID);
    mouse_read(); // ACK
    uint8_t device_id = mouse_read();
    
    // Device ID 3 means IntelliMouse with scroll wheel
    // Device ID 4 means IntelliMouse with 5 buttons
    if (device_id == 3 || device_id == 4) {
        has_scroll_wheel = 1;
    }
}

void mouse_init(void) {
    // Enable auxiliary mouse device
    mouse_wait_input();
    outb(MOUSE_COMMAND_PORT, CONTROLLER_CMD_ENABLE_AUX);
    
    // Get current configuration
    mouse_wait_input();
    outb(MOUSE_COMMAND_PORT, CONTROLLER_CMD_READ_CONFIG);
    mouse_wait_output();
    uint8_t status = inb(MOUSE_DATA_PORT);
    
    // Enable IRQ12 (mouse interrupt) and clear other flags
    status |= 0x02;  // Enable mouse interrupt
    status &= ~0x20; // Enable mouse clock
    
    // Write configuration back
    mouse_wait_input();
    outb(MOUSE_COMMAND_PORT, CONTROLLER_CMD_WRITE_CONFIG);
    mouse_wait_input();
    outb(MOUSE_DATA_PORT, status);
    
    // Set mouse defaults
    mouse_write(MOUSE_CMD_SET_DEFAULTS);
    mouse_read(); // ACK
    
    // Try to enable scroll wheel
    mouse_try_enable_scrollwheel();
    
    // Enable packet streaming
    mouse_write(MOUSE_CMD_ENABLE_PACKET_STREAMING);
    mouse_read(); // ACK
    
    // Initialize state
    mouse_cycle = 0;
    new_data_available = 0;
}

/**
 * Process a byte from the mouse (should be called from IRQ12 handler)
 */
void mouse_handle_interrupt(uint8_t mouse_byte) {
    mouse_bytes[mouse_cycle] = mouse_byte;
    mouse_cycle++;
    
    // Standard mouse: 3 bytes, IntelliMouse with scroll: 4 bytes
    uint8_t packet_size = has_scroll_wheel ? 4 : 3;
    
    if (mouse_cycle >= packet_size) {
        mouse_cycle = 0;
        
        // Store previous packet
        previous_packet = current_packet;
        
        // Parse the packet
        current_packet.buttons = mouse_bytes[0] & 0x07;
        current_packet.x_movement = mouse_bytes[1];
        current_packet.y_movement = mouse_bytes[2];
        
        // Handle scroll wheel if available
        if (has_scroll_wheel) {
            current_packet.z_movement = (int8_t)mouse_bytes[3];
        } else {
            current_packet.z_movement = 0;
        }
        
        // Check for X/Y overflow bits
        if (mouse_bytes[0] & 0x40) current_packet.x_movement = 0;
        if (mouse_bytes[0] & 0x80) current_packet.y_movement = 0;
        
        new_data_available = 1;
    }
}

/**
 * Poll the mouse for new data (use if IRQ12 is not set up)
 * Returns 1 if mouse data was processed, 0 otherwise
 */
void mouse_poll(void) {
    uint8_t status = inb(MOUSE_COMMAND_PORT);
    
    // Check if data is available (bit 0) AND it's from mouse (bit 5)
    // Bit 5 = 1 means data is from auxiliary device (mouse)
    // Bit 5 = 0 means data is from keyboard
    if ((status & 0x01) && (status & 0x20)) {
        uint8_t mouse_byte = inb(MOUSE_DATA_PORT);
        
        // Validate first byte of packet (bit 3 should always be set)
        if (mouse_cycle == 0 && !(mouse_byte & 0x08)) {
            // Invalid first byte, discard and reset
            return;
        }
        
        mouse_handle_interrupt(mouse_byte);
    }
}

mouse_packet_t* mouse_get_packet(void) {
    if (new_data_available) {
        new_data_available = 0;
        return &current_packet;
    }
    return 0;
}

uint8_t mouse_has_data(void) {
    return new_data_available;
}

uint8_t mouse_get_events(void) {
    uint8_t events = 0;
    
    // Check for movement
    if (current_packet.x_movement != 0 || current_packet.y_movement != 0) {
        events |= MOUSE_EVENT_MOVE;
    }
    
    // Check for button states
    if (current_packet.buttons & 0x01) events |= MOUSE_EVENT_LEFT_BTN;
    if (current_packet.buttons & 0x02) events |= MOUSE_EVENT_RIGHT_BTN;
    if (current_packet.buttons & 0x04) events |= MOUSE_EVENT_MIDDLE_BTN;
    
    // Check for scroll wheel
    if (current_packet.z_movement > 0) {
        events |= MOUSE_EVENT_SCROLL_UP;
    } else if (current_packet.z_movement < 0) {
        events |= MOUSE_EVENT_SCROLL_DOWN;
    }
    
    return events;
}
