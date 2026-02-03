/*
 * === AOS HEADER BEGIN ===
 * ./include/shell.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

// Shell configuration
#define SHELL_PROMPT_MAX 64
#define SHELL_INPUT_MAX 256
#define SHELL_HISTORY_MAX 100
#define SHELL_HISTORY_FILE ".shhistory"

/**
 * Initialize shell subsystem
 */
void shell_init(void);

/**
 * Display login prompt and handle authentication
 * @return 0 on successful login, negative on error
 */
int shell_login(void);

/**
 * Run interactive shell for logged-in user
 */
void shell_run(void);

/**
 * Display shell prompt
 */
void shell_display_prompt(void);

/**
 * Process a shell command
 * @param command Command string to process
 * @return 0 on success, -1 on failure
 */
int shell_process_command(const char* command);

/**
 * Exit the shell
 */
void shell_exit(void);

/**
 * Check if command execution should be cancelled (Ctrl+C pressed)
 * @return 1 if cancelled, 0 otherwise
 */
int shell_is_cancelled(void);

/**
 * Clear the cancellation flag
 */
void shell_clear_cancel(void);

/**
 * Check if shell should exit
 * @return 1 if should exit, 0 otherwise
 */
int shell_should_exit(void);

/**
 * Check and handle scheduled shutdown
 */
void shell_check_scheduled_shutdown(void);

/**
 * Read password input with masking
 * @param buffer Buffer to store password
 * @param max_len Maximum buffer length
 * @return Number of characters read, or -1 on error
 */
int read_password(char* buffer, uint32_t max_len);

/**
 * Load shell history from user's history file
 */
void shell_load_history(void);

/**
 * Save shell history to user's history file
 */
void shell_save_history(void);

/**
 * Add command to shell history
 * @param command Command to add
 */
void shell_add_history(const char* command);

#endif // SHELL_H
