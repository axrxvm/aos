/*
 * === AOS HEADER BEGIN ===
 * include/krm.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef KRM_H
#define KRM_H

#include <stdint.h>
#include <arch/i386/isr.h>

// Kernel Recovery Mode (KRM) - Standalone panic recovery system
// KRM is completely independent of VFS, networking, and other aOS components
// It provides a minimal interface for viewing panic information and rebooting

#define KRM_MAX_MESSAGE_LEN 256
#define KRM_MAX_FILE_LEN 128
#define KRM_MAX_BACKTRACE_FRAMES 16
#define KRM_MAX_EXPLANATION_LEN 512
#define KRM_MAX_SUGGESTIONS 4
#define KRM_MAX_SUGGESTION_LEN 128

// Panic information structure
typedef struct {
    char message[KRM_MAX_MESSAGE_LEN];
    char file[KRM_MAX_FILE_LEN];
    uint32_t line;
    registers_t registers;  // Full register dump
    uint8_t has_registers;  // Whether register state is available
    uint32_t backtrace[KRM_MAX_BACKTRACE_FRAMES];  // EIP addresses in stack trace
    uint32_t backtrace_count;  // Number of valid backtrace entries
    uint32_t panic_time;  // Time of panic (in ticks, if available)
    char explanation[KRM_MAX_EXPLANATION_LEN];  // User-friendly explanation
    char suggestions[KRM_MAX_SUGGESTIONS][KRM_MAX_SUGGESTION_LEN];  // Suggested actions
    uint32_t suggestion_count;  // Number of suggestions
} krm_panic_info_t;

// KRM Menu Options
typedef enum {
    KRM_MENU_VIEW_EXPLANATION = 0,
    KRM_MENU_VIEW_DETAILS = 1,
    KRM_MENU_VIEW_BACKTRACE = 2,
    KRM_MENU_VIEW_REGISTERS = 3,
    KRM_MENU_REBOOT = 4,
    KRM_MENU_HALT = 5,
    KRM_MENU_COUNT = 6
} krm_menu_option_t;

// Enter Kernel Recovery Mode with panic information
// This function does NOT return - it takes over the system
// Protected against cascading panics - if called while already in panic,
// will halt immediately instead of re-entering KRM
void krm_enter(registers_t *regs, const char *message, const char *file, uint32_t line);

// Initialize KRM (called early in boot, before most subsystems)
void krm_init(void);

// Check if system is currently in panic/KRM state
// Returns 1 if in panic, 0 otherwise
uint8_t krm_is_in_panic(void);

#endif // KRM_H
