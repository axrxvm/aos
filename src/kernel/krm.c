/*
 * === AOS HEADER BEGIN ===
 * src/kernel/krm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <krm.h>
#include <io.h>


// KRM - Kernel Recovery Mode
// A standalone, minimal recovery interface for kernel panics
// Does NOT depend on VFS, networking, memory allocators, or other subsystems


// VGA Text Mode Constants (Direct hardware access)
#define KRM_VGA_WIDTH 80
#define KRM_VGA_HEIGHT 25
#define KRM_VGA_MEMORY ((uint16_t*)0xB8000)

// VGA Colors
#define KRM_COLOR_BLACK 0
#define KRM_COLOR_BLUE 1
#define KRM_COLOR_GREEN 2
#define KRM_COLOR_CYAN 3
#define KRM_COLOR_RED 4
#define KRM_COLOR_MAGENTA 5
#define KRM_COLOR_BROWN 6
#define KRM_COLOR_LIGHT_GREY 7
#define KRM_COLOR_DARK_GREY 8
#define KRM_COLOR_LIGHT_BLUE 9
#define KRM_COLOR_LIGHT_GREEN 10
#define KRM_COLOR_LIGHT_CYAN 11
#define KRM_COLOR_LIGHT_RED 12
#define KRM_COLOR_LIGHT_MAGENTA 13
#define KRM_COLOR_YELLOW 14
#define KRM_COLOR_WHITE 15

#define KRM_MAKE_COLOR(fg, bg) ((bg << 4) | (fg))
#define KRM_COLOR_PANIC KRM_MAKE_COLOR(KRM_COLOR_WHITE, KRM_COLOR_RED)
#define KRM_COLOR_NORMAL KRM_MAKE_COLOR(KRM_COLOR_LIGHT_GREY, KRM_COLOR_BLACK)
#define KRM_COLOR_SELECTED KRM_MAKE_COLOR(KRM_COLOR_BLACK, KRM_COLOR_LIGHT_GREY)
#define KRM_COLOR_HEADER KRM_MAKE_COLOR(KRM_COLOR_YELLOW, KRM_COLOR_BLUE)
#define KRM_COLOR_INFO KRM_MAKE_COLOR(KRM_COLOR_LIGHT_CYAN, KRM_COLOR_BLACK)

// Keyboard scancodes (PS/2 Set 1)
#define KRM_KEY_UP 0x48
#define KRM_KEY_DOWN 0x50
#define KRM_KEY_ENTER 0x1C
#define KRM_KEY_ESC 0x01

// PS/2 Controller Ports
#define KRM_KB_DATA_PORT 0x60
#define KRM_KB_STATUS_PORT 0x64
#define KRM_KB_COMMAND_PORT 0x64

// Serial Ports
#define KRM_SERIAL_PORT 0x3F8
#define KRM_SERIAL_LINE_STATUS 0x3FD

// Static storage for panic information (no dynamic allocation)
static krm_panic_info_t krm_panic_data;
static uint8_t krm_initialized = 0;

// Current menu state
static krm_menu_option_t krm_current_menu = KRM_MENU_VIEW_DETAILS;

// Panic guard - prevents cascading panics
static volatile uint8_t krm_panic_in_progress = 0;


// Minimal String Functions (standalone, no dependencies)


static uint32_t krm_strlen(const char *str) {
    uint32_t len = 0;
    while (str && str[len]) len++;
    return len;
}

static void krm_strcpy(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0;
    while (src && src[i] && i < max_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void krm_memset(void *ptr, uint8_t value, uint32_t size) {
    uint8_t *p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = value;
    }
}

// Convert uint32 to hex string (standalone)
static void krm_uint_to_hex(uint32_t value, char *buf, uint32_t buf_size) {
    const char hex_chars[] = "0123456789ABCDEF";
    if (buf_size < 11) return; // Need at least "0x12345678\0"
    
    buf[0] = '0';
    buf[1] = 'x';
    
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex_chars[(value >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
}

// Convert uint32 to decimal string (standalone)
static void krm_uint_to_dec(uint32_t value, char *buf, uint32_t buf_size) {
    if (buf_size < 12) return; // Max uint32 is 10 digits + sign + null
    
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    
    char temp[12];
    int i = 0;
    
    while (value > 0 && i < 11) {
        temp[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    // Reverse
    for (int j = 0; j < i; j++) {
        buf[j] = temp[i - 1 - j];
    }
    buf[i] = '\0';
}


// Minimal VGA Driver 


static void krm_vga_putchar_at(char c, uint8_t color, uint32_t x, uint32_t y) {
    if (x >= KRM_VGA_WIDTH || y >= KRM_VGA_HEIGHT) return;
    uint32_t index = y * KRM_VGA_WIDTH + x;
    KRM_VGA_MEMORY[index] = (uint16_t)c | ((uint16_t)color << 8);
}

static void krm_vga_clear(uint8_t color) {
    for (uint32_t y = 0; y < KRM_VGA_HEIGHT; y++) {
        for (uint32_t x = 0; x < KRM_VGA_WIDTH; x++) {
            krm_vga_putchar_at(' ', color, x, y);
        }
    }
}

static void krm_vga_write_string_at(const char *str, uint8_t color, uint32_t x, uint32_t y) {
    uint32_t i = 0;
    while (str && str[i] && x < KRM_VGA_WIDTH) {
        if (str[i] == '\n') {
            y++;
            x = 0;
            i++;
            continue;
        }
        krm_vga_putchar_at(str[i], color, x, y);
        x++;
        i++;
    }
}

static void krm_vga_fill_line(uint32_t y, uint8_t color, char fill_char) {
    for (uint32_t x = 0; x < KRM_VGA_WIDTH; x++) {
        krm_vga_putchar_at(fill_char, color, x, y);
    }
}


// Minimal Serial Driver 


static void krm_serial_init(void) {
    // Initialize COM1 (0x3F8) for KRM serial output
    outb(KRM_SERIAL_PORT + 1, 0x00);    // Disable all interrupts
    outb(KRM_SERIAL_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(KRM_SERIAL_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(KRM_SERIAL_PORT + 1, 0x00);    //                  (hi byte)
    outb(KRM_SERIAL_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(KRM_SERIAL_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(KRM_SERIAL_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static void krm_serial_write_char(char c) {
    // Wait for transmit buffer to be empty
    while (!(inb(KRM_SERIAL_LINE_STATUS) & 0x20));
    outb(KRM_SERIAL_PORT, c);
}

static void krm_serial_write_string(const char* str) {
    if (!str) return;
    while (*str) {
        krm_serial_write_char(*str);
        str++;
    }
}


// Minimal Keyboard Driver (Polling, no IRQ, standalone)


static void krm_kb_wait_output_ready(void) {
    // Wait for keyboard controller output buffer to be ready
    uint32_t timeout = 100000;
    while (timeout-- && (inb(KRM_KB_STATUS_PORT) & 0x02));
}

static void krm_kb_wait_input_ready(void) {
    // Wait for keyboard controller input buffer to have data
    uint32_t timeout = 100000;
    while (timeout-- && !(inb(KRM_KB_STATUS_PORT) & 0x01));
}

static void krm_kb_flush_buffer(void) {
    // Flush the keyboard controller buffer
    uint32_t timeout = 1000;
    while (timeout-- && (inb(KRM_KB_STATUS_PORT) & 0x01)) {
        inb(KRM_KB_DATA_PORT); // Read and discard
    }
}

static void krm_kb_init(void) {
    krm_serial_write_string("[KRM] Initializing keyboard controller...\n");
    
    // Disable devices
    krm_kb_wait_output_ready();
    outb(KRM_KB_COMMAND_PORT, 0xAD); // Disable first PS/2 port
    krm_kb_wait_output_ready();
    outb(KRM_KB_COMMAND_PORT, 0xA7); // Disable second PS/2 port (if exists)
    
    // Flush buffer
    krm_kb_flush_buffer();
    
    // Read controller configuration byte
    krm_kb_wait_output_ready();
    outb(KRM_KB_COMMAND_PORT, 0x20); // Read command byte
    krm_kb_wait_input_ready();
    uint8_t config = inb(KRM_KB_DATA_PORT);
    
    // Modify configuration: enable interrupts and scancode translation
    config |= 0x01;  // Enable first port interrupt
    config &= ~0x10; // Clear first port clock (enable)
    
    // Write configuration back
    krm_kb_wait_output_ready();
    outb(KRM_KB_COMMAND_PORT, 0x60); // Write command byte
    krm_kb_wait_output_ready();
    outb(KRM_KB_DATA_PORT, config);
    
    // Enable first PS/2 port
    krm_kb_wait_output_ready();
    outb(KRM_KB_COMMAND_PORT, 0xAE); // Enable first PS/2 port
    
    // Flush buffer again
    krm_kb_flush_buffer();
    
    // Reset keyboard
    krm_kb_wait_output_ready();
    outb(KRM_KB_DATA_PORT, 0xFF); // Reset command
    krm_kb_wait_input_ready();
    uint8_t response = inb(KRM_KB_DATA_PORT);
    
    if (response == 0xFA || response == 0xAA) {
        krm_serial_write_string("[KRM] Keyboard initialized successfully\n");
    } else {
        krm_serial_write_string("[KRM] Keyboard init: unexpected response\n");
    }
    
    // Final buffer flush
    krm_kb_flush_buffer();
}

static uint8_t krm_kb_get_scancode(void) {
    // Check if data is available (non-blocking)
    if (!(inb(KRM_KB_STATUS_PORT) & 0x01)) {
        return 0; // No data available
    }
    return inb(KRM_KB_DATA_PORT);
}

static uint8_t krm_kb_wait_scancode(void) {
    // Wait for a scancode (blocking with timeout)
    uint32_t timeout = 0xFFFFFF; // Large timeout
    
    while (timeout--) {
        if (inb(KRM_KB_STATUS_PORT) & 0x01) {
            return inb(KRM_KB_DATA_PORT);
        }
    }
    
    return 0; // Timeout
}

static uint8_t krm_kb_wait_key_press(void) {
    // Wait for a key press (ignore release scancodes)
    while (1) {
        uint8_t scancode = krm_kb_wait_scancode();
        if (scancode == 0) return 0; // Timeout
        
        // Ignore release scancodes (bit 7 set)
        if (!(scancode & 0x80)) {
            return scancode;
        }
    }
}

static void krm_kb_wait_key_release(uint8_t press_scancode) {
    // Wait for the key to be released
    uint8_t release_code = press_scancode | 0x80;
    uint32_t timeout = 0xFFFFFF;
    
    while (timeout--) {
        uint8_t scancode = krm_kb_get_scancode();
        if (scancode == release_code) {
            return;
        }
    }
}


// Panic Explanation System


static void krm_add_suggestion(const char* suggestion) {
    if (krm_panic_data.suggestion_count >= KRM_MAX_SUGGESTIONS) return;
    krm_strcpy(krm_panic_data.suggestions[krm_panic_data.suggestion_count],
               suggestion, KRM_MAX_SUGGESTION_LEN);
    krm_panic_data.suggestion_count++;
}

static void krm_analyze_panic(void) {
    krm_panic_data.suggestion_count = 0;
    
    // Clear explanation buffer
    krm_memset(krm_panic_data.explanation, 0, KRM_MAX_EXPLANATION_LEN);
    
    // Analyze based on register state and message
    if (krm_panic_data.has_registers) {
        uint32_t int_no = krm_panic_data.registers.int_no;
        uint32_t err_code = krm_panic_data.registers.err_code;
        uint32_t eip = krm_panic_data.registers.eip;
        uint32_t cr2 = 0; // Page fault address (would need to be captured)
        
        switch (int_no) {
            case 0: // Divide by Zero
                krm_strcpy(krm_panic_data.explanation,
                    "Division by Zero Error: The CPU attempted to divide a number by zero, which is mathematically undefined. This typically occurs when a variable used as a divisor was unexpectedly zero.",
                    KRM_MAX_EXPLANATION_LEN);
                krm_add_suggestion("Check division operations near the fault address");
                krm_add_suggestion("Verify loop counters and array indices");
                krm_add_suggestion("Add validation before division operations");
                break;
                
            case 6: // Invalid Opcode
                krm_strcpy(krm_panic_data.explanation,
                    "Invalid Opcode: The CPU encountered an instruction it doesn't recognize. This usually means the instruction pointer is pointing to invalid code, often due to memory corruption, jumping to data instead of code, or stack overflow.",
                    KRM_MAX_EXPLANATION_LEN);
                krm_add_suggestion("Check for buffer overflows corrupting code");
                krm_add_suggestion("Verify function pointers are not corrupted");
                krm_add_suggestion("Inspect stack for overflow conditions");
                break;
                
            case 8: // Double Fault
                krm_strcpy(krm_panic_data.explanation,
                    "Double Fault: An exception occurred while trying to handle a previous exception. This is a critical error indicating severe system instability, often caused by stack problems or corrupted exception handlers.",
                    KRM_MAX_EXPLANATION_LEN);
                krm_add_suggestion("Check kernel stack size and overflow");
                krm_add_suggestion("Verify IDT and exception handler integrity");
                krm_add_suggestion("Inspect TSS and stack segment setup");
                break;
                
            case 13: // General Protection Fault
                krm_strcpy(krm_panic_data.explanation,
                    "General Protection Fault: The CPU detected a privilege violation or illegal memory access. This can occur from null pointer dereferences, accessing invalid segments, or violating memory protection rules.",
                    KRM_MAX_EXPLANATION_LEN);
                
                if (err_code != 0) {
                    if (err_code & 0x1) {
                        krm_add_suggestion("External event caused fault (check hardware)");
                    }
                    if (err_code & 0x2) {
                        krm_add_suggestion("Fault in IDT - check interrupt handlers");
                    } else if (err_code & 0x4) {
                        krm_add_suggestion("Fault in LDT - check local descriptors");
                    } else {
                        krm_add_suggestion("Fault in GDT - check segment selectors");
                    }
                } else {
                    krm_add_suggestion("Check for null pointer dereferences");
                    krm_add_suggestion("Verify segment selectors are valid");
                }
                krm_add_suggestion("Inspect memory access at fault address");
                break;
                
            case 14: // Page Fault
                krm_strcpy(krm_panic_data.explanation,
                    "Page Fault: Attempted to access memory that is not mapped or violates page permissions. The error code indicates the type of access that failed.",
                    KRM_MAX_EXPLANATION_LEN);
                
                if (err_code & 0x1) {
                    krm_add_suggestion("Page protection violation - access denied");
                } else {
                    krm_add_suggestion("Page not present - unmapped memory access");
                }
                
                if (err_code & 0x2) {
                    krm_add_suggestion("Write access failed - check write permissions");
                } else {
                    krm_add_suggestion("Read access failed - check page mapping");
                }
                
                if (err_code & 0x4) {
                    krm_add_suggestion("User-mode access - check privilege levels");
                } else {
                    krm_add_suggestion("Kernel-mode access - check kernel pointers");
                }
                break;
                
            default:
                if (int_no < 32) {
                    krm_strcpy(krm_panic_data.explanation,
                        "CPU Exception: A hardware exception was triggered by the processor. This indicates a serious error in kernel execution that violated CPU protection mechanisms.",
                        KRM_MAX_EXPLANATION_LEN);
                    krm_add_suggestion("Check kernel code near fault address");
                    krm_add_suggestion("Verify memory and stack integrity");
                    krm_add_suggestion("Review recent kernel changes");
                } else {
                    krm_strcpy(krm_panic_data.explanation,
                        "Interrupt Handler Panic: An interrupt handler encountered a critical error and triggered a kernel panic to prevent system corruption.",
                        KRM_MAX_EXPLANATION_LEN);
                    krm_add_suggestion("Check interrupt handler code");
                    krm_add_suggestion("Verify device driver stability");
                    krm_add_suggestion("Inspect hardware interrupt behavior");
                }
                break;
        }
    } else {
        // Software panic - analyze message
        const char* msg = krm_panic_data.message;
        
        // Check for common panic messages
        if (msg[0] == 'A' && msg[1] == 's' && msg[2] == 's') { // "Assertion"
            krm_strcpy(krm_panic_data.explanation,
                "The system found something unexpected and stopped to prevent damage. The kernel has built-in safety checks, and one of them failed - meaning the system was in a state it shouldn't be in.",
                KRM_MAX_EXPLANATION_LEN);
            krm_add_suggestion("This is a safety check that caught a problem");
            krm_add_suggestion("Look at what the check was testing");
            krm_add_suggestion("There's likely a bug in the kernel code");
        }
        else if ((msg[0] == 'O' && msg[1] == 'u' && msg[2] == 't') || // "Out of"
                 (msg[0] == 'o' && msg[1] == 'u' && msg[2] == 't')) {
            krm_strcpy(krm_panic_data.explanation,
                "The system ran out of something it needs to operate (probably memory). Like running out of paper when printing, the system couldn't get the resources it needed to continue working.",
                KRM_MAX_EXPLANATION_LEN);
            krm_add_suggestion("Too many things were running at once");
            krm_add_suggestion("A program might be using too much memory");
            krm_add_suggestion("The system might need more memory allocated");
        }
        else if (msg[0] == 'V' && msg[1] == 'F' && msg[2] == 'S') { // "VFS"
            krm_strcpy(krm_panic_data.explanation,
                "There was a serious problem with the file system. The system couldn't read or write files properly, which could mean the disk is corrupted or a file operation went wrong.",
                KRM_MAX_EXPLANATION_LEN);
            krm_add_suggestion("The disk might be corrupted or full");
            krm_add_suggestion("A file operation failed unexpectedly");
            krm_add_suggestion("Try checking the filesystem for errors");
        }
        else if (msg[0] == 'N' || msg[0] == 'n') { // Could be "Network" or "NULL"
            if (msg[1] == 'e' || msg[1] == 'E') { // "Network"
                krm_strcpy(krm_panic_data.explanation,
                    "The network system encountered a fatal error. This could be a problem with the network hardware, driver, or the network software itself crashed.",
                    KRM_MAX_EXPLANATION_LEN);
                krm_add_suggestion("Network hardware might be malfunctioning");
                krm_add_suggestion("Network driver may have a bug");
                krm_add_suggestion("Try without network devices attached");
            } else { // Likely "NULL"
                krm_strcpy(krm_panic_data.explanation,
                    "The system tried to use something that doesn't exist yet or was never set up. It's like trying to use a tool that's not in your toolbox - the program expected something to be there, but it wasn't.",
                    KRM_MAX_EXPLANATION_LEN);
                krm_add_suggestion("Something wasn't initialized properly");
                krm_add_suggestion("A program tried to use data that doesn't exist");
                krm_add_suggestion("Check if things are started in the right order");
            }
        }
        else {
            // Generic software panic
            krm_strcpy(krm_panic_data.explanation,
                "The kernel detected a problem that made it unsafe to continue running. The system stopped itself on purpose to prevent data corruption or other damage. This is better than continuing with unknown problems.",
                KRM_MAX_EXPLANATION_LEN);
            krm_add_suggestion("Read the panic message for specific clues");
            krm_add_suggestion("Check what was happening before the crash");
            krm_add_suggestion("This might be a bug that needs fixing");
        }
    }
    
    // Add universal suggestion to report the issue
    krm_add_suggestion("Report this at https://github.com/axrxvm/aos/issues");
    
    krm_serial_write_string("[KRM] Panic analysis complete\n");
    krm_serial_write_string("[KRM] Explanation: ");
    krm_serial_write_string(krm_panic_data.explanation);
    krm_serial_write_string("\n");
}


// Backtrace Collection (Called before entering KRM UI)


// Safe memory validation - check if address is in plausible kernel range
static uint8_t krm_is_valid_kernel_addr(uintptr_t addr) {
    // Kernel code/data is typically in low memory (0x100000 - 0x1000000)
    // Stack is also in this range
    // This is a heuristic check to avoid page faults
    
    if (addr == 0) return 0; // NULL pointer
    if (addr < 0x1000) return 0; // Too low, likely invalid
    if (addr > 0x10000000) return 0; // Too high, likely invalid
    
    // Check if it's in identity-mapped region (0-8MB is safe in aOS)
    if (addr >= 0x100000 && addr < 0x800000) return 1;
    
    // Reject everything else to be safe
    return 0;
}

static void krm_collect_backtrace(uint32_t *backtrace, uint32_t *count, uint32_t max_frames) {
    *count = 0;
    uintptr_t* frame = (uintptr_t*)__builtin_frame_address(0);
    
    krm_serial_write_string("[KRM] Starting backtrace from EBP: 0x");
    char addr_buf[16];
    krm_uint_to_hex((uint32_t)(uintptr_t)frame, addr_buf, sizeof(addr_buf));
    krm_serial_write_string(addr_buf);
    krm_serial_write_string("\n");
    
    for (uint32_t depth = 0; frame && depth < max_frames; depth++) {
        // Validate EBP before dereferencing
        if (!krm_is_valid_kernel_addr((uintptr_t)frame)) {
            krm_serial_write_string("[KRM] Invalid EBP, stopping backtrace\n");
            break;
        }
        
        // Validate EBP is properly aligned (should be 4-byte aligned)
        if (((uintptr_t)frame) & 0x3) {
            krm_serial_write_string("[KRM] Misaligned EBP, stopping backtrace\n");
            break;
        }
        
        // Validate we can read [ebp+4] safely
        uintptr_t eip_addr = (uintptr_t)&frame[1];
        if (!krm_is_valid_kernel_addr(eip_addr)) {
            krm_serial_write_string("[KRM] Cannot read return address, stopping backtrace\n");
            break;
        }
        
        uint32_t eip = (uint32_t)frame[1]; // Return address at [ebp+4]
        
        // Validate EIP is in code range
        if (!krm_is_valid_kernel_addr(eip)) {
            krm_serial_write_string("[KRM] Invalid return address, stopping backtrace\n");
            break;
        }
        
        if (eip == 0) {
            krm_serial_write_string("[KRM] Null return address, end of trace\n");
            break;
        }
        
        // Store valid backtrace entry
        backtrace[*count] = eip;
        (*count)++;
        
        // Get previous EBP
        uintptr_t prev_ebp_addr = (uintptr_t)&frame[0];
        if (!krm_is_valid_kernel_addr(prev_ebp_addr)) {
            krm_serial_write_string("[KRM] Cannot read previous EBP, stopping backtrace\n");
            break;
        }
        
        uintptr_t prev_ebp_val = frame[0]; // Previous EBP at [ebp]
        
        // Validate previous EBP
        if (!krm_is_valid_kernel_addr(prev_ebp_val)) {
            krm_serial_write_string("[KRM] Previous EBP invalid, stopping backtrace\n");
            break;
        }
        
        // Check for loops
        if (prev_ebp_val == (uintptr_t)frame) {
            krm_serial_write_string("[KRM] Loop detected in stack, stopping backtrace\n");
            break;
        }
        
        // Stack grows downward, so prev_ebp should be higher than current
        if (prev_ebp_val < (uintptr_t)frame && depth > 0) {
            krm_serial_write_string("[KRM] Stack direction anomaly, stopping backtrace\n");
            break;
        }
        
        frame = (uintptr_t*)prev_ebp_val;
    }
    
    krm_serial_write_string("[KRM] Backtrace complete\n");
}



// KRM UI Screens



static void krm_draw_header(const char *title) {
    krm_vga_fill_line(0, KRM_COLOR_HEADER, ' ');
    uint32_t title_len = krm_strlen(title);
    uint32_t start_x = (KRM_VGA_WIDTH - title_len) / 2;
    krm_vga_write_string_at(title, KRM_COLOR_HEADER, start_x, 0);
}

static void krm_draw_footer(void) {
    krm_vga_fill_line(KRM_VGA_HEIGHT - 1, KRM_COLOR_INFO, ' ');
    krm_vga_write_string_at("Use UP/DOWN arrows to navigate, ENTER to select, ESC to return", 
                           KRM_COLOR_INFO, 2, KRM_VGA_HEIGHT - 1);
}

static void krm_screen_explanation(void) {
    krm_vga_clear(KRM_COLOR_NORMAL);
    krm_draw_header("=== WHAT HAPPENED? ===");
    
    uint32_t y = 2;
    
    // Title
    krm_vga_write_string_at("Panic Analysis", KRM_COLOR_INFO, 2, y);
    y += 2;
    
    // Explanation (word wrap for long text)
    krm_vga_write_string_at("Explanation:", KRM_COLOR_INFO, 2, y++);
    
    const char* explanation = krm_panic_data.explanation;
    uint32_t line_start = 0;
    uint32_t char_count = 0;
    uint32_t max_line_width = KRM_VGA_WIDTH - 6;
    
    while (explanation[char_count] && y < KRM_VGA_HEIGHT - 6) {
        // Find word boundaries for wrapping
        uint32_t line_len = 0;
        uint32_t last_space = 0;
        
        while (explanation[char_count] && line_len < max_line_width) {
            if (explanation[char_count] == ' ') {
                last_space = line_len;
            }
            char_count++;
            line_len++;
        }
        
        // If we hit max width mid-word, backtrack to last space
        if (explanation[char_count] && last_space > 0) {
            char_count = line_start + last_space + 1;
            line_len = last_space;
        }
        
        // Print line
        for (uint32_t i = 0; i < line_len && explanation[line_start + i]; i++) {
            if (explanation[line_start + i] != ' ' || i > 0) {
                krm_vga_putchar_at(explanation[line_start + i], KRM_COLOR_NORMAL, 4 + i, y);
            }
        }
        
        y++;
        line_start = char_count;
    }
    
    y++;
    
    // Suggestions
    if (krm_panic_data.suggestion_count > 0) {
        krm_vga_write_string_at("What might help:", KRM_COLOR_INFO, 2, y++);
        y++;
        
        for (uint32_t i = 0; i < krm_panic_data.suggestion_count && y < KRM_VGA_HEIGHT - 1; i++) {
            krm_vga_write_string_at("* ", KRM_COLOR_INFO, 4, y);
            krm_vga_write_string_at(krm_panic_data.suggestions[i], KRM_COLOR_NORMAL, 6, y);
            y++;
        }
    }
    
    krm_draw_footer();
}

static void krm_screen_panic_details(void) {
    krm_vga_clear(KRM_COLOR_NORMAL);
    krm_draw_header("=== KERNEL PANIC - DETAILS ===");
    
    char buf[64];
    uint32_t y = 2;
    
    // Panic banner
    krm_vga_fill_line(y, KRM_COLOR_PANIC, '!');
    krm_vga_write_string_at("   KERNEL PANIC   ", KRM_COLOR_PANIC, 31, y);
    y += 2;
    
    // Message
    krm_vga_write_string_at("Message:", KRM_COLOR_INFO, 2, y++);
    krm_vga_write_string_at(krm_panic_data.message, KRM_COLOR_NORMAL, 4, y++);
    y++;
    
    // Location
    krm_vga_write_string_at("Location:", KRM_COLOR_INFO, 2, y++);
    krm_vga_write_string_at(krm_panic_data.file, KRM_COLOR_NORMAL, 4, y++);
    krm_vga_write_string_at("Line: ", KRM_COLOR_INFO, 4, y);
    krm_uint_to_dec(krm_panic_data.line, buf, sizeof(buf));
    krm_vga_write_string_at(buf, KRM_COLOR_NORMAL, 10, y);
    y += 2;
    
    // Backtrace summary
    if (krm_panic_data.backtrace_count > 0) {
        krm_vga_write_string_at("Backtrace frames: ", KRM_COLOR_INFO, 2, y);
        krm_uint_to_dec(krm_panic_data.backtrace_count, buf, sizeof(buf));
        krm_vga_write_string_at(buf, KRM_COLOR_NORMAL, 20, y);
        y++;
    }
    
    // Register availability
    if (krm_panic_data.has_registers) {
        krm_vga_write_string_at("Register dump available", KRM_COLOR_INFO, 2, y);
    } else {
        krm_vga_write_string_at("Register dump not available (software panic)", KRM_COLOR_NORMAL, 2, y);
    }
    
    krm_draw_footer();
}

static void krm_screen_backtrace(void) {
    krm_vga_clear(KRM_COLOR_NORMAL);
    krm_draw_header("=== STACK BACKTRACE ===");
    
    char buf[32];
    uint32_t y = 2;
    
    if (krm_panic_data.backtrace_count == 0) {
        krm_vga_write_string_at("No backtrace available", KRM_COLOR_NORMAL, 2, y);
    } else {
        krm_vga_write_string_at("Call stack (most recent first):", KRM_COLOR_INFO, 2, y);
        y += 2;
        
        for (uint32_t i = 0; i < krm_panic_data.backtrace_count && y < KRM_VGA_HEIGHT - 2; i++) {
            // Frame number
            krm_uint_to_dec(i, buf, sizeof(buf));
            krm_vga_write_string_at("#", KRM_COLOR_INFO, 2, y);
            krm_vga_write_string_at(buf, KRM_COLOR_INFO, 3, y);
            krm_vga_write_string_at(": ", KRM_COLOR_INFO, 5, y);
            
            // Address
            krm_uint_to_hex(krm_panic_data.backtrace[i], buf, sizeof(buf));
            krm_vga_write_string_at(buf, KRM_COLOR_NORMAL, 8, y);
            y++;
        }
    }
    
    krm_draw_footer();
}

static void krm_screen_registers(void) {
    krm_vga_clear(KRM_COLOR_NORMAL);
    krm_draw_header("=== REGISTER DUMP ===");
    
    char buf[32];
    uint32_t y = 2;
    
    if (!krm_panic_data.has_registers) {
        krm_vga_write_string_at("Register state not available (software panic)", KRM_COLOR_NORMAL, 2, y);
        krm_draw_footer();
        return;
    }
    
    registers_t *r = &krm_panic_data.registers;
    
    // General purpose registers
    krm_vga_write_string_at("General Purpose Registers:", KRM_COLOR_INFO, 2, y);
    y += 2;
    
    #define PRINT_REG(name, value, x_pos, y_pos) \
        krm_vga_write_string_at(name, KRM_COLOR_INFO, x_pos, y_pos); \
        krm_uint_to_hex(value, buf, sizeof(buf)); \
        krm_vga_write_string_at(buf, KRM_COLOR_NORMAL, x_pos + 8, y_pos);
    
    PRINT_REG("EAX:   ", r->eax, 2, y);
    PRINT_REG("EBX:   ", r->ebx, 30, y);
    y++;
    PRINT_REG("ECX:   ", r->ecx, 2, y);
    PRINT_REG("EDX:   ", r->edx, 30, y);
    y++;
    PRINT_REG("ESI:   ", r->esi, 2, y);
    PRINT_REG("EDI:   ", r->edi, 30, y);
    y++;
    PRINT_REG("EBP:   ", r->ebp, 2, y);
    
    uint32_t esp = r->esp_dummy;
    if ((r->cs & 0x3) != 0) esp = r->useresp;
    PRINT_REG("ESP:   ", esp, 30, y);
    y += 2;
    
    // Program counter & segments
    krm_vga_write_string_at("Program Counter & Segments:", KRM_COLOR_INFO, 2, y);
    y += 2;
    
    PRINT_REG("EIP:   ", r->eip, 2, y);
    PRINT_REG("CS:    ", r->cs, 30, y);
    y++;
    PRINT_REG("DS:    ", r->ds, 2, y);
    PRINT_REG("SS:    ", r->ss, 30, y);
    y += 2;
    
    // Flags & interrupt info
    krm_vga_write_string_at("Flags & Interrupt Info:", KRM_COLOR_INFO, 2, y);
    y += 2;
    
    PRINT_REG("EFLAGS:", r->eflags, 2, y);
    PRINT_REG("INT#:  ", r->int_no, 30, y);
    y++;
    
    // Error code (if applicable)
    if (r->int_no == 8 || (r->int_no >= 10 && r->int_no <= 14) || 
        r->int_no == 17 || r->int_no == 30) {
        PRINT_REG("ERRCODE:", r->err_code, 2, y);
    }
    
    #undef PRINT_REG
    
    krm_draw_footer();
}

static void krm_screen_menu(void) {
    krm_vga_clear(KRM_COLOR_NORMAL);
    krm_draw_header("=== aOS KERNEL RECOVERY MODE (KRM) ===");
    
    uint32_t y = 3;
    
    krm_vga_write_string_at("The kernel has encountered a fatal error and cannot continue.", 
                           KRM_COLOR_NORMAL, 5, y);
    y++;
    krm_vga_write_string_at("Please select an option below:", KRM_COLOR_NORMAL, 5, y);
    y += 3;
    
    // Menu options
    const char* menu_items[] = {
        "What Happened?",
        "View Panic Details",
        "View Stack Backtrace",
        "View Register Dump",
        "Reboot System",
        "Halt System"
    };
    
    for (uint32_t i = 0; i < KRM_MENU_COUNT; i++) {
        uint8_t color = (i == krm_current_menu) ? KRM_COLOR_SELECTED : KRM_COLOR_NORMAL;
        char prefix[4] = "   ";
        if (i == krm_current_menu) {
            prefix[0] = '>';
            prefix[1] = ' ';
        }
        
        krm_vga_write_string_at(prefix, color, 10, y);
        krm_vga_write_string_at(menu_items[i], color, 13, y);
        
        // Fill rest of line for selected item
        if (i == krm_current_menu) {
            for (uint32_t x = 13 + krm_strlen(menu_items[i]); x < KRM_VGA_WIDTH - 10; x++) {
                krm_vga_putchar_at(' ', color, x, y);
            }
        }
        y++;
    }
    
    krm_draw_footer();
}


// System Control Functions


static void krm_reboot(void) {
    krm_vga_clear(KRM_COLOR_NORMAL);
    krm_vga_write_string_at("Rebooting...", KRM_COLOR_INFO, 34, 12);
    
    // Try keyboard controller reboot
    outb(0x64, 0xFE);
    
    // If that fails, try triple fault
    asm volatile("cli");
    asm volatile("lidt (0)");
    asm volatile("int $0x03");
    
    // Infinite loop as fallback
    while(1) asm volatile("hlt");
}

static void krm_halt(void) {
    krm_vga_clear(KRM_COLOR_NORMAL);
    krm_vga_write_string_at("System Halted. It is now safe to power off.", 
                           KRM_COLOR_INFO, 18, 12);
    
    asm volatile("cli");
    while(1) asm volatile("hlt");
}



// Main KRM Event Loop


static void krm_main_loop(void) {
    uint8_t in_submenu = 0;
    
    krm_serial_write_string("[KRM] Entering main event loop\n");
    
    while (1) {
        // Draw current screen
        if (in_submenu) {
            switch (krm_current_menu) {
                case KRM_MENU_VIEW_EXPLANATION:
                    krm_screen_explanation();
                    break;
                case KRM_MENU_VIEW_DETAILS:
                    krm_screen_panic_details();
                    break;
                case KRM_MENU_VIEW_BACKTRACE:
                    krm_screen_backtrace();
                    break;
                case KRM_MENU_VIEW_REGISTERS:
                    krm_screen_registers();
                    break;
                default:
                    in_submenu = 0;
                    continue;
            }
        } else {
            krm_screen_menu();
        }
        
        // Wait for keypress using improved polling
        uint8_t scancode = krm_kb_wait_key_press();
        
        if (scancode == 0) {
            // Timeout occurred, continue loop
            continue;
        }
        
        // Wait for key release to avoid repeat
        krm_kb_wait_key_release(scancode);
        
        // Handle input
        if (in_submenu) {
            if (scancode == KRM_KEY_ESC) {
                in_submenu = 0;
            }
        } else {
            switch (scancode) {
                case KRM_KEY_UP:
                    if (krm_current_menu > 0) {
                        krm_current_menu--;
                    }
                    break;
                    
                case KRM_KEY_DOWN:
                    if (krm_current_menu < KRM_MENU_COUNT - 1) {
                        krm_current_menu++;
                    }
                    break;
                    
                case KRM_KEY_ENTER:
                    switch (krm_current_menu) {
                        case KRM_MENU_VIEW_EXPLANATION:
                        case KRM_MENU_VIEW_DETAILS:
                        case KRM_MENU_VIEW_BACKTRACE:
                        case KRM_MENU_VIEW_REGISTERS:
                            in_submenu = 1;
                            break;
                        case KRM_MENU_REBOOT:
                            krm_reboot();
                            break;
                        case KRM_MENU_HALT:
                            krm_halt();
                            break;
                    }
                    break;
            }
        }
    }
}


// Public KRM Interface


void krm_init(void) {
    krm_memset(&krm_panic_data, 0, sizeof(krm_panic_data));
    krm_panic_in_progress = 0;
    krm_initialized = 1;
}

uint8_t krm_is_in_panic(void) {
    return krm_panic_in_progress;
}

void krm_enter(registers_t *regs, const char *message, const char *file, uint32_t line) {
    // Disable interrupts - we're in recovery mode now
    asm volatile("cli");
    
    // CRITICAL: Check for cascading panic (panic within panic)
    // If we're already in panic mode, don't try to enter KRM again
    // This prevents infinite panic loops
    if (krm_panic_in_progress) {
        // Double fault / cascading panic detected!
        // Write minimal error message directly to VGA and halt immediately
        uint16_t* vga = (uint16_t*)0xB8000;
        const char* msg = "*** DOUBLE PANIC - CASCADING FAULT ***";
        uint8_t color = KRM_MAKE_COLOR(KRM_COLOR_WHITE, KRM_COLOR_RED);
        
        // Clear screen with red background
        for (int i = 0; i < 80 * 25; i++) {
            vga[i] = ' ' | (color << 8);
        }
        
        // Write error message in center
        int len = 0;
        while (msg[len]) len++;
        int start_x = (80 - len) / 2;
        int y = 12;
        
        for (int i = 0; msg[i]; i++) {
            vga[y * 80 + start_x + i] = msg[i] | (color << 8);
        }
        
        // Write to serial if possible
        const char* serial_msg = "\n\n*** CRITICAL: DOUBLE PANIC DETECTED ***\n";
        const char* serial_msg2 = "Panic occurred while handling another panic!\n";
        const char* serial_msg3 = "System halted to prevent infinite panic loop.\n\n";
        
        // Direct serial output (bypass any functions that might panic)
        for (const char* s = serial_msg; *s; s++) {
            while (!(inb(0x3FD) & 0x20)); // Wait for TX ready
            outb(0x3F8, *s);
        }
        for (const char* s = serial_msg2; *s; s++) {
            while (!(inb(0x3FD) & 0x20));
            outb(0x3F8, *s);
        }
        for (const char* s = serial_msg3; *s; s++) {
            while (!(inb(0x3FD) & 0x20));
            outb(0x3F8, *s);
        }
        
        // Halt immediately - no further recovery attempts
        while(1) asm volatile("hlt");
    }
    
    // Set panic guard to prevent re-entry
    krm_panic_in_progress = 1;
    
    // Initialize KRM's own serial and keyboard (independent of main kernel's)
    krm_serial_init();
    krm_serial_write_string("\n\n");
    krm_serial_write_string("==============================================\n");
    krm_serial_write_string("   aOS KERNEL RECOVERY MODE (KRM) ACTIVATED\n");
    krm_serial_write_string("==============================================\n");
    krm_serial_write_string("[KRM] Initializing standalone hardware drivers...\n");
    
    krm_kb_init();
    
    krm_serial_write_string("[KRM] Hardware initialization complete\n");
    
    // Store panic information
    krm_strcpy(krm_panic_data.message, message ? message : "(null)", KRM_MAX_MESSAGE_LEN);
    krm_strcpy(krm_panic_data.file, file ? file : "(unknown)", KRM_MAX_FILE_LEN);
    krm_panic_data.line = line;
    
    krm_serial_write_string("[KRM] Panic message: ");
    krm_serial_write_string(krm_panic_data.message);
    krm_serial_write_string("\n[KRM] Location: ");
    krm_serial_write_string(krm_panic_data.file);
    krm_serial_write_string(":");
    char line_buf[16];
    krm_uint_to_dec(line, line_buf, sizeof(line_buf));
    krm_serial_write_string(line_buf);
    krm_serial_write_string("\n");
    
    // Store register state if available
    if (regs) {
        krm_panic_data.registers = *regs;
        krm_panic_data.has_registers = 1;
        krm_serial_write_string("[KRM] Register state captured\n");
    } else {
        krm_panic_data.has_registers = 0;
        krm_serial_write_string("[KRM] No register state available (software panic)\n");
    }
    
    // Collect backtrace
    krm_serial_write_string("[KRM] Collecting stack backtrace...\n");
    krm_collect_backtrace(krm_panic_data.backtrace, &krm_panic_data.backtrace_count, 
                         KRM_MAX_BACKTRACE_FRAMES);
    
    char count_buf[16];
    krm_uint_to_dec(krm_panic_data.backtrace_count, count_buf, sizeof(count_buf));
    krm_serial_write_string("[KRM] Collected ");
    krm_serial_write_string(count_buf);
    krm_serial_write_string(" stack frames\n");
    
    // Analyze panic and generate intelligent explanation
    krm_serial_write_string("[KRM] Analyzing panic...\n");
    krm_analyze_panic();
    
    // Reset menu to default (explanation screen)
    krm_current_menu = KRM_MENU_VIEW_EXPLANATION;
    
    krm_serial_write_string("[KRM] Starting user interface...\n");
    
    // Enter the main KRM loop (NEVER RETURNS)
    krm_main_loop();
    
    // Should never reach here, but just in case
    krm_serial_write_string("[KRM] ERROR: Main loop returned! We are doomed to misery...\n");
    while(1) asm volatile("hlt");
}
