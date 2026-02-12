/*
 * === AOS HEADER BEGIN ===
 * src/userspace/shell/shell.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <shell.h>
#include <user.h>
#include <command.h>
#include <keyboard.h>
#include <dev/mouse.h>
#include <vga.h>
#include <serial.h>
#include <string.h>
#include <stdlib.h>
#include <fs/vfs.h>
#include <version.h>
#include <crypto/sha256.h>
#include <fs_layout.h>
#include <acpi.h>
#include <io.h>

static int shell_exit_flag = 0;
static int shell_cancel_flag = 0;
static char input_buffer[SHELL_INPUT_MAX];
static uint32_t input_pos = 0;
static uint32_t cursor_pos = 0;  // Current cursor position within input
static uint8_t input_start_col = 0;  // Column where input starts on screen
static uint8_t input_start_row = 0;  // Row where input starts on screen

// Shell history
static char history_buffer[SHELL_HISTORY_MAX][SHELL_INPUT_MAX];
static uint32_t history_count = 0;
static int32_t history_position = -1;  // -1 = not navigating, 0+ = index in history
static char current_input_backup[SHELL_INPUT_MAX];  // Backup of current input when navigating

// Externals for shutdown checking
extern volatile uint32_t system_ticks;
extern volatile uint32_t shutdown_scheduled_tick;
extern volatile uint32_t shutdown_message_last_tick;
extern void kprint(const char *str);
extern int unformatted_disk_detected;

#define PIT_BASE_FREQUENCY 1193182
#define PIT_DEFAULT_DIVISOR 11932

// Helper function to redraw the input line with proper cursor positioning
static void redraw_input_line(void) {
    // Move to start of input
    vga_set_position(input_start_row, input_start_col);
    
    // Clear to end of line by writing spaces (need to clear old content)
    for (uint8_t i = 0; i < VGA_WIDTH - input_start_col; i++) {
        vga_putc(' ');
    }
    
    // Go back to start of input
    vga_set_position(input_start_row, input_start_col);
    
    // Redraw the entire input buffer
    for (uint32_t i = 0; i < input_pos; i++) {
        vga_putc(input_buffer[i]);
    }
    
    // Position cursor at correct location
    vga_set_position(input_start_row, input_start_col + cursor_pos);
}

void shell_load_history(void) {
    session_t* session = user_get_session();
    if (!session || !session->user) {
        return;
    }
    
    // Build history file path: home_dir/.shhistory
    char history_path[256];
    memset(history_path, 0, sizeof(history_path));
    strncpy(history_path, session->user->home_dir, sizeof(history_path) - 1);
    
    // Add trailing slash if not present
    uint32_t len = strlen(history_path);
    if (len > 0 && history_path[len - 1] != '/') {
        strncat(history_path, "/", sizeof(history_path) - len - 1);
    }
    strncat(history_path, SHELL_HISTORY_FILE, sizeof(history_path) - strlen(history_path) - 1);
    
    // Reset history
    history_count = 0;
    history_position = -1;
    memset(history_buffer, 0, sizeof(history_buffer));
    
    // Try to open history file
    int fd = vfs_open(history_path, 0);
    if (fd < 0) {
        // File doesn't exist, that's okay for first time
        return;
    }
    
    // Read file content
    char file_buffer[SHELL_INPUT_MAX * 10];  // Buffer for reading
    int bytes_read = vfs_read(fd, file_buffer, sizeof(file_buffer) - 1);
    
    // Validate bytes_read
    if (bytes_read < 0) {
        vfs_close(fd);
        return;
    }
    
    if (bytes_read >= (int)sizeof(file_buffer)) {
        bytes_read = sizeof(file_buffer) - 1;
    }
    
    if (bytes_read > 0) {
        file_buffer[bytes_read] = '\0';
        
        // Parse lines
        char* line_start = file_buffer;
        for (uint32_t i = 0; i < (uint32_t)bytes_read && history_count < SHELL_HISTORY_MAX; i++) {
            if (file_buffer[i] == '\n') {
                file_buffer[i] = '\0';
                size_t line_len = strlen(line_start);
                if (line_len > 0 && line_len < SHELL_INPUT_MAX) {
                    strncpy(history_buffer[history_count], line_start, SHELL_INPUT_MAX - 1);
                    history_buffer[history_count][SHELL_INPUT_MAX - 1] = '\0';
                    history_count++;
                }
                line_start = &file_buffer[i + 1];
            }
        }
        
        // Handle last line if no trailing newline
        size_t last_line_len = strlen(line_start);
        if (last_line_len > 0 && last_line_len < SHELL_INPUT_MAX && history_count < SHELL_HISTORY_MAX) {
            strncpy(history_buffer[history_count], line_start, SHELL_INPUT_MAX - 1);
            history_buffer[history_count][SHELL_INPUT_MAX - 1] = '\0';
            history_count++;
        }
    }
    
    vfs_close(fd);
}

void shell_save_history(void) {
    session_t* session = user_get_session();
    if (!session || !session->user) {
        return;
    }
    
    // Build history file path
    char history_path[256];
    memset(history_path, 0, sizeof(history_path));
    
    // Validate home_dir length
    size_t home_len = strlen(session->user->home_dir);
    if (home_len > 240) { // Leave space for filename
        serial_puts("SHELL: home_dir too long\n");
        return;
    }
    
    strncpy(history_path, session->user->home_dir, sizeof(history_path) - 1);
    
    // Add trailing slash if not present
    uint32_t len = strlen(history_path);
    if (len > 0 && history_path[len - 1] != '/') {
        strncat(history_path, "/", sizeof(history_path) - len - 1);
    }
    strncat(history_path, SHELL_HISTORY_FILE, sizeof(history_path) - strlen(history_path) - 1);
    
    // Build file content in memory first
    char file_content[SHELL_HISTORY_MAX * SHELL_INPUT_MAX];
    memset(file_content, 0, sizeof(file_content));
    uint32_t content_len = 0;
    
    for (uint32_t i = 0; i < history_count; i++) {
        const char* line = history_buffer[i];
        uint32_t line_len = strlen(line);
        
        // Copy line
        memcpy(file_content + content_len, line, line_len);
        content_len += line_len;
        
        // Add newline
        file_content[content_len++] = '\n';
    }
    
    // Try to open/create file for writing
    int fd = vfs_open(history_path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        // Try to create the file first
        vnode_t* parent = vfs_resolve_path(session->user->home_dir);
        if (parent && parent->ops && parent->ops->create) {
            if (parent->ops->create(parent, SHELL_HISTORY_FILE, VFS_FILE) == 0) {
                fd = vfs_open(history_path, 0);
            }
        }
    }
    
    if (fd < 0) {
        return;  // Failed to open/create
    }
    
    // Write content
    if (content_len > 0) {
        vfs_write(fd, file_content, content_len);
    }
    
    vfs_close(fd);
}

void shell_add_history(const char* command) {
    if (!command || !*command) {
        return;
    }
    
    // Don't add if it's the same as the last command
    if (history_count > 0 && strcmp(history_buffer[history_count - 1], command) == 0) {
        return;
    }
    
    // If history is full, shift everything down
    if (history_count >= SHELL_HISTORY_MAX) {
        for (uint32_t i = 0; i < SHELL_HISTORY_MAX - 1; i++) {
            strncpy(history_buffer[i], history_buffer[i + 1], SHELL_INPUT_MAX - 1);
        }
        history_count = SHELL_HISTORY_MAX - 1;
    }
    
    // Add new entry
    strncpy(history_buffer[history_count], command, SHELL_INPUT_MAX - 1);
    history_buffer[history_count][SHELL_INPUT_MAX - 1] = '\0';
    history_count++;
    
    // Save to file
    shell_save_history();
}

void shell_init(void) {
    serial_puts("Initializing shell...\n");
    shell_exit_flag = 0;
    shell_cancel_flag = 0;
    input_pos = 0;
    memset(input_buffer, 0, sizeof(input_buffer));
    serial_puts("Shell initialized.\n");
}

int shell_is_cancelled(void) {
    return shell_cancel_flag;
}

void shell_clear_cancel(void) {
    shell_cancel_flag = 0;
}

static void shell_set_cancel(void) {
    shell_cancel_flag = 1;
}

int read_password(char* buffer, uint32_t max_len) {
    uint32_t pos = 0;
    memset(buffer, 0, max_len);
    
    // Flush keyboard buffer before starting
    keyboard_flush_buffer();
    
    // Enable blinking cursor for password input
    vga_set_cursor_style(CURSOR_BLINK);
    vga_enable_cursor();
    
    while (1) {
        // Small delay for keyboard controller
        for (volatile int i = 0; i < 1000; i++);
        
        uint8_t scancode = keyboard_get_scancode();
        if (scancode == 0) {
            continue; // No key pressed
        }
        
        char c = scancode_to_char(scancode);
        
        if (c == '\n') {
            // Enter pressed
            buffer[pos] = '\0';
            vga_putc('\n');
            return pos;
        } else if (c == '\b') {
            // Backspace
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                vga_putc('\b');
            }
        } else if (c >= 32 && c <= 126 && pos < max_len - 1) {
            // Printable character
            buffer[pos++] = c;
            vga_putc('*'); // Display asterisk instead of actual character
        }
    }
}

static void read_line(char* buffer, uint32_t max_len) {
    uint32_t pos = 0;
    memset(buffer, 0, max_len);
    
    // Flush keyboard buffer before starting
    keyboard_flush_buffer();
    
    // Enable blinking cursor for command input
    vga_set_cursor_style(CURSOR_BLINK);
    vga_enable_cursor();
    
    while (1) {
        // Small delay for keyboard controller
        for (volatile int i = 0; i < 1000; i++);
        
        uint8_t scancode = keyboard_get_scancode();
        if (scancode == 0) {
            continue; // No key pressed
        }
        
        char c = scancode_to_char(scancode);
        
        // Check for Ctrl+C
        if (keyboard_is_ctrl_pressed() && (c == 'c' || c == 'C')) {
            vga_putc('^');
            vga_putc('C');
            vga_putc('\n');
            buffer[0] = '\0';
            return;
        }
        
        if (c == '\n') {
            // Enter pressed
            buffer[pos] = '\0';
            vga_putc('\n');
            return;
        } else if (c == '\b') {
            // Backspace
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                vga_putc('\b');
            }
        } else if (c == KEY_LEFT) {
            // Left arrow - move cursor left in buffer
            if (pos > 0) {
                pos--;
                vga_putc('\b');
            }
        } else if (c == KEY_RIGHT) {
            // Right arrow - move cursor right in buffer (only if there's text)
            if (pos < strlen(buffer)) {
                vga_putc(buffer[pos++]);
            }
        } else if (c >= 32 && c <= 126 && pos < max_len - 1) {
            // Printable character
            buffer[pos++] = c;
            vga_putc(c);
        }
    }
}

int shell_login(void) {
    char username[32];
    char password[64];
    char new_password[64];
    int attempts = 0;
    const int max_attempts = 3;
    
    vga_clear_all();
    
    // Display red header banner
    vga_set_color(0x0C); // Light red on black
    vga_puts("================================================================================\n");
    vga_puts("                              aOS LOGIN SYSTEM                                 \n");
    vga_puts("================================================================================\n");
    vga_set_color(0x0F); // White on black
    vga_puts("\n");
    
    // Display version and system info
    vga_set_color(0x07); // Light gray
    vga_puts("                         Welcome to aOS v");
    vga_puts(AOS_VERSION_SHORT);
    vga_puts("\n");
    vga_puts("                    A Modern i386 Operating System\n");
    vga_set_color(0x0F); // White
    vga_puts("\n");
    
    // Check if this is first-time setup (root with default password)
    user_t* root = user_find_by_name("root");
    int first_time = 0;
    if (root) {
        // Check if root still has default password
        char default_hash[MAX_PASSWORD_HASH];
        sha256_ctx_t ctx;
        uint8_t digest[32];
        sha256_init(&ctx);
        sha256_update(&ctx, (const uint8_t*)"root", 4);
        sha256_final(&ctx, digest);
        sha256_to_hex(digest, default_hash);
        if (strcmp(root->password_hash, default_hash) == 0) {
            first_time = 1;
        }
    }
    
    if (first_time) {
        vga_set_color(0x0E); // Yellow on black
        vga_puts("                          FIRST TIME SETUP\n");
        vga_puts("                          ================\n\n");
        vga_set_color(0x0F); // White
        vga_puts("  Welcome! Please login with the default credentials:\n\n");
        vga_set_color(0x0A); // Light green
        vga_puts("    Username: ");
        vga_set_color(0x0B); // Light cyan
        vga_puts("root\n");
        vga_set_color(0x0A); // Light green
        vga_puts("    Password: ");
        vga_set_color(0x0B); // Light cyan
        vga_puts("root\n\n");
        vga_set_color(0x0F); // White
        vga_puts("  You will be prompted to set a new password after login.\n\n");
    }
    
    vga_set_color(0x08); // Dark gray
    vga_puts("--------------------------------------------------------------------------------\n");
    vga_set_color(0x0F); // White
    vga_puts("\n");
    
    while (attempts < max_attempts) {
        vga_set_color(0x0A); // Light green
        vga_puts("Username: ");
        vga_set_color(0x0F); // White
        read_line(username, sizeof(username));
        
        vga_set_color(0x0A); // Light green
        vga_puts("Password: ");
        vga_set_color(0x0F); // White
        read_password(password, sizeof(password));
        
        // Authenticate user
        user_t* user = user_authenticate(username, password);
        if (user) {
            // Login successful
            user_login(user);
            
            vga_puts("\n");
            vga_set_color(0x0A); // Light green
            vga_puts("Login successful! Welcome, ");
            vga_set_color(0x0B); // Light cyan
            vga_puts(username);
            vga_set_color(0x0A); // Light green
            vga_puts("!\n\n");
            vga_set_color(0x0F); // White
            
            // Check if user must change password (for newly created users)
            if ((user->flags & USER_FLAG_MUST_CHANGE_PASS) && !first_time) {
                vga_set_color(0x0E); // Yellow
                vga_puts("You must change your password before continuing.\n\n");
                vga_set_color(0x0F); // White
                
                while (1) {
                    vga_set_color(0x0A); // Light green
                    vga_puts("New password: ");
                    vga_set_color(0x0F); // White
                    read_password(new_password, sizeof(new_password));
                    
                    if (strlen(new_password) < 4) {
                        vga_set_color(0x0C); // Light red
                        vga_puts("\nPassword too short (minimum 4 characters). Try again.\n");
                        vga_set_color(0x0F); // White
                        continue;
                    }
                    
                    vga_set_color(0x0A); // Light green
                    vga_puts("Confirm password: ");
                    vga_set_color(0x0F); // White
                    char confirm[64];
                    read_password(confirm, sizeof(confirm));
                    
                    if (strcmp(new_password, confirm) != 0) {
                        vga_set_color(0x0C); // Light red
                        vga_puts("\nPasswords do not match. Try again.\n\n");
                        vga_set_color(0x0F); // White
                        continue;
                    }
                    
                    // Change password
                    if (user_set_password(username, new_password) == 0) {
                        // Clear the flag
                        user->flags &= ~USER_FLAG_MUST_CHANGE_PASS;
                        
                        vga_set_color(0x0A); // Light green
                        vga_puts("\nPassword changed successfully!\n");
                        vga_set_color(0x0F); // White
                        
                        // Save to database if in LOCAL mode
                        if (fs_layout_get_mode() == FS_MODE_LOCAL) {
                            if (user_save_database(USER_DATABASE_PATH) == 0) {
                                serial_puts("User database saved after mandatory password change\n");
                            }
                        }
                        vga_puts("\n");
                        break;
                    } else {
                        vga_set_color(0x0C); // Light red
                        vga_puts("\nFailed to change password. Try again.\n\n");
                        vga_set_color(0x0F); // White
                    }
                }
            }
            
            // Check if this is first login with default password (root only)
            if (first_time && strcmp(username, "root") == 0) {
                vga_set_color(0x0E); // Yellow
                vga_puts("Please set a new password for security.\n");
                vga_set_color(0x0A); // Light green
                vga_puts("New password: ");
                vga_set_color(0x0F); // White
                read_password(new_password, sizeof(new_password));
                
                if (strlen(new_password) < 4) {
                    vga_set_color(0x0C); // Light red
                    vga_puts("Password too short (minimum 4 characters).\n");
                    vga_puts("Keeping default password. Change it later.\n\n");
                    vga_set_color(0x0F); // White
                } else {
                    vga_set_color(0x0A); // Light green
                    vga_puts("Confirm password: ");
                    vga_set_color(0x0F); // White
                    char confirm[64];
                    read_password(confirm, sizeof(confirm));
                    
                    if (strcmp(new_password, confirm) == 0) {
                        user_set_password("root", new_password);
                        vga_set_color(0x0A); // Light green
                        vga_puts("Password changed successfully!\n");
                        vga_set_color(0x0F); // White
                        
                        // Save database immediately if in local mode
                        if (fs_layout_get_mode() == FS_MODE_LOCAL) {
                            vga_puts("Saving to disk...\n");
                            if (user_save_database(USER_DATABASE_PATH) == 0) {
                                vga_puts("Changes saved.\n\n");
                            } else {
                                vga_set_color(0x0C); // Light red
                                vga_puts("Warning: Could not save to disk!\n\n");
                                vga_set_color(0x0F); // White
                            }
                        } else {
                            vga_set_color(0x08); // Dark gray
                            vga_puts("(Running in ISO mode - changes will not persist)\n\n");
                            vga_set_color(0x0F); // White
                        }
                    } else {
                        vga_set_color(0x0C); // Light red
                        vga_puts("Passwords do not match.\n");
                        vga_puts("Keeping default password. Change it later.\n\n");
                        vga_set_color(0x0F); // White
                    }
                    memset(confirm, 0, sizeof(confirm));
                }
                memset(new_password, 0, sizeof(new_password));
            }
            
            return 0;
        } else {
            attempts++;
            vga_set_color(0x0C); // Light red
            vga_puts("Login incorrect. ");
            if (attempts < max_attempts) {
                vga_puts("Please try again.\n\n");
            } else {
                vga_puts("Maximum attempts reached.\n");
            }
            vga_set_color(0x0F); // White
        }
        
        // Clear password buffer for security
        memset(password, 0, sizeof(password));
    }
    
    return -1;
}

void shell_display_prompt(void) {
    session_t* session = user_get_session();
    if (!session || !session->user) {
        vga_puts("$ ");
        return;
    }
    
    // Format: [user]@aOS:[path]#
    vga_set_color(0x0A); // Light green
    vga_puts("[");
    vga_puts(session->user->username);
    vga_puts("@aOS:");
    
    // Display working directory
    const char* cwd = vfs_getcwd();
    vga_set_color(0x0B); // Cyan
    
    // If in home directory, display ~
    if (strncmp(cwd, session->user->home_dir, strlen(session->user->home_dir)) == 0) {
        vga_puts("~");
        const char* subdir = cwd + strlen(session->user->home_dir);
        if (*subdir) {
            vga_puts(subdir);
        }
    } else {
        vga_puts(cwd);
    }
    
    vga_set_color(0x0A); // Light green
    vga_puts("]");
    
    // Display prompt character (# for root, $ for regular users)
    if (user_is_root()) {
        vga_set_color(0x0C); // Light red for root
        vga_puts("# ");
    } else {
        vga_set_color(0x0A); // Light green for users
        vga_puts("$ ");
    }
    vga_set_color(0x0F); // Back to white for input
}

int shell_process_command(const char* command) {
    if (!command || !*command) {
        return -1;
    }
    
    // Check for built-in shell commands
    if (strcmp(command, "exit") == 0 || strcmp(command, "logout") == 0) {
        shell_exit();
        return 0;
    }
    
    if (strcmp(command, "whoami") == 0) {
        session_t* session = user_get_session();
        if (session && session->user) {
            vga_puts("You are currently logged in as:");
            vga_puts(session->user->username);
            vga_putc('\n');
        }
        return 0;
    }
    
    // Pass to command system
    return execute_command(command);
}

void shell_run(void) {
    session_t* session = user_get_session();
    if (!session || !(session->session_flags & SESSION_FLAG_LOGGED_IN)) {
        vga_puts("Error: Not logged in\n");
        return;
    }
    
    // Clear screen AND scrollback buffer completely before displaying ASCII art
    vga_clear_all(); // this func does exactly that
    
    // Display ASCII art banner
    vga_set_color(0x02); // Green text on black background
    vga_puts("         ___  ____  \n");
    vga_puts("   __ _ / _ \\/ ___| \n");
    vga_puts("  / _` | | | \\___ \\ \n");
    vga_puts(" | (_| | |_| |___) |\n");
    vga_puts("  \\__,_|\\___/|____/ \n");
    vga_puts("                    \n");
    vga_set_color(0x0F); // White text on black background
    vga_puts("Welcome to aOS!\n");
    // vga_puts("Version: ");
    vga_puts(AOS_VERSION);
    vga_puts("\n\n");
    
    // Display information about unformatted disk if detected
    if (unformatted_disk_detected) {
        vga_set_color(0x0E); // Yellow for information
        vga_puts("[INFO] Unformatted disk detected!\n");
        vga_set_color(0x0F); // White
        vga_puts("To use the disk for persistent storage, run the 'format' command.\n");
        vga_puts("Note: This will prepare the disk with the SimpleFS filesystem.\n\n");
        unformatted_disk_detected = 0;  // Clear flag after displaying
    }
    
    // Display filesystem mode
   // int fs_mode = fs_layout_get_mode();
  //  if (fs_mode == FS_MODE_LOCAL) {
  //      vga_set_color(0x0A); // Light green
 //       vga_puts("[LOCAL MODE]");
  //  } else {
 //       vga_set_color(0x0E); // Yellow
//        vga_puts("[ISO MODE]");
 //   }
 //   vga_set_color(0x0F); // White
   // vga_puts(" Storage: ");
   // vga_puts(fs_mode == FS_MODE_LOCAL ? "SimpleFS on disk\n" : "ramfs in memory\n");
   // vga_puts("\nType 'help' for available commands.\n\n");
    
    shell_exit_flag = 0;
    
    // Load command history
    shell_load_history();
    
    while (!shell_exit_flag) {
        shell_display_prompt();
        
        // Enable blinking cursor for input
        vga_enable_cursor();
        vga_set_cursor_style(CURSOR_UNDERLINE);
        
        // Read command
        input_pos = 0;
        cursor_pos = 0;  // Reset cursor position
        history_position = -1;  // Reset history navigation
        memset(input_buffer, 0, sizeof(input_buffer));
        memset(current_input_backup, 0, sizeof(current_input_backup));
        
        // Store where input begins on screen
        input_start_row = vga_get_row();
        input_start_col = vga_get_col();
        
        while (1) {
            // Check for scheduled shutdown
            shell_check_scheduled_shutdown();
            
            // Poll mouse for scroll wheel events first
            mouse_poll();
            if (mouse_has_data()) {
                mouse_packet_t* packet = mouse_get_packet();
                if (packet && packet->z_movement != 0) {
                    if (packet->z_movement > 0) {
                        vga_scroll_up_view(); // Scroll view up to see older content
                    } else {
                        vga_scroll_down(); // Scroll view down to see newer content
                    }
                }
                continue; // Check for more input
            }
            
            uint8_t scancode = keyboard_get_scancode();
            if (scancode == 0) {
                continue; // No key pressed
            }
            
            char c = scancode_to_char(scancode);
            
            // Check for Ctrl+C
            if (keyboard_is_ctrl_pressed() && (c == 'c' || c == 'C')) {
                vga_putc('^');
                vga_putc('C');
                vga_putc('\n');
                shell_set_cancel();
                input_pos = 0;
                cursor_pos = 0;
                input_buffer[0] = '\0';
                history_position = -1;
                break;
            }
            
            // Handle UP arrow - navigate to older commands
            if (c == KEY_UP) {
                if (history_count == 0) {
                    continue;  // No history
                }
                
                // First time pressing up - save current input
                if (history_position == -1) {
                    strncpy(current_input_backup, input_buffer, SHELL_INPUT_MAX - 1);
                    history_position = history_count - 1;
                } else if (history_position > 0) {
                    history_position--;
                }
                
                // Clear current line - backspace over all characters
                while (input_pos > 0) {
                    vga_putc('\b');
                    input_pos--;
                }
                
                // Load history entry
                strncpy(input_buffer, history_buffer[history_position], SHELL_INPUT_MAX - 1);
                input_buffer[SHELL_INPUT_MAX - 1] = '\0';
                input_pos = strlen(input_buffer);
                cursor_pos = input_pos;  // Cursor at end
                
                // Display it
                vga_puts(input_buffer);
                continue;
            }
            
            // Handle DOWN arrow - navigate to newer commands
            if (c == KEY_DOWN) {
                if (history_position == -1) {
                    continue;  // Not navigating history
                }
                
                // Clear current line
                while (input_pos > 0) {
                    vga_putc('\b');
                    input_pos--;
                }
                
                if (history_position < (int32_t)(history_count - 1)) {
                    // Move to next command
                    history_position++;
                    strncpy(input_buffer, history_buffer[history_position], SHELL_INPUT_MAX - 1);
                    input_buffer[SHELL_INPUT_MAX - 1] = '\0';
                    input_pos = strlen(input_buffer);
                    cursor_pos = input_pos;  // Cursor at end
                    vga_puts(input_buffer);
                } else {
                    // Reached the end - restore original input
                    history_position = -1;
                    strncpy(input_buffer, current_input_backup, SHELL_INPUT_MAX - 1);
                    input_buffer[SHELL_INPUT_MAX - 1] = '\0';
                    input_pos = strlen(input_buffer);
                    cursor_pos = input_pos;  // Cursor at end
                    if (input_pos > 0) {
                        vga_puts(input_buffer);
                    }
                }
                continue;
            }
            
            // Handle LEFT arrow - move cursor left
            if (c == KEY_LEFT) {
                if (cursor_pos > 0) {
                    cursor_pos--;
                    // Move VGA cursor back one position
                    uint8_t row = vga_get_row();
                    uint8_t col = vga_get_col();
                    if (col > 0) {
                        vga_set_position(row, col - 1);
                    } else if (row > 0) {
                        // Wrap to previous line
                        vga_set_position(row - 1, VGA_WIDTH - 1);
                    }
                }
                continue;
            }
            
            // Handle RIGHT arrow - move cursor right
            if (c == KEY_RIGHT) {
                if (cursor_pos < input_pos) {
                    cursor_pos++;
                    // Move VGA cursor forward one position
                    uint8_t row = vga_get_row();
                    uint8_t col = vga_get_col();
                    if (col < VGA_WIDTH - 1) {
                        vga_set_position(row, col + 1);
                    } else if (row < VGA_HEIGHT - 1) {
                        // Wrap to next line
                        vga_set_position(row + 1, 0);
                    }
                }
                continue;
            }
            
            if (c == '\n') {
                // Enter pressed
                input_buffer[input_pos] = '\0';
                vga_putc('\n');
                break;
            } else if (c == '\b') {
                // Backspace - delete character before cursor
                if (cursor_pos > 0) {
                    if (cursor_pos == input_pos) {
                        // Simple case: cursor at end, just backspace
                        input_pos--;
                        cursor_pos--;
                        input_buffer[input_pos] = '\0';
                        vga_backspace();  // Use VGA's built-in backspace
                    } else {
                        // Complex case: cursor in middle, need to shift and redraw
                        // Shift characters left
                        for (uint32_t i = cursor_pos - 1; i < input_pos - 1; i++) {
                            input_buffer[i] = input_buffer[i + 1];
                        }
                        input_pos--;
                        cursor_pos--;
                        input_buffer[input_pos] = '\0';
                        
                        // Redraw the line
                        redraw_input_line();
                    }
                    
                    // Reset history navigation if user modifies command
                    history_position = -1;
                }
            } else if (c >= 32 && c <= 126 && input_pos < SHELL_INPUT_MAX - 1) {
                // Printable character - insert at cursor position
                if (cursor_pos < input_pos) {
                    // Insert mode - shift characters right
                    for (uint32_t i = input_pos; i > cursor_pos; i--) {
                        input_buffer[i] = input_buffer[i - 1];
                    }
                }
                input_buffer[cursor_pos] = c;
                input_pos++;
                cursor_pos++;
                input_buffer[input_pos] = '\0';
                
                // Redraw the line if we inserted in the middle
                if (cursor_pos < input_pos) {
                    redraw_input_line();
                } else {
                    // Just append at the end
                    vga_putc(c);
                }
                
                // Reset history navigation if user modifies command
                history_position = -1;
            }
        }
        
        // Process command if not cancelled
        if (!shell_cancel_flag && input_pos > 0) {
            // Execute command and only add to history if successful
            int result = shell_process_command(input_buffer);
            if (result == 0) {
                shell_add_history(input_buffer);
            }
        }
        
        // Clear cancel flag for next command
        shell_clear_cancel();
    }
    
    // Logout
    user_logout();
}

void shell_exit(void) {
    shell_exit_flag = 1;
    vga_puts("Logging out...\n");
}

int shell_should_exit(void) {
    return shell_exit_flag;
}

void shell_check_scheduled_shutdown(void) {
    if (shutdown_scheduled_tick == 0) {
        return; // No shutdown scheduled
    }
    
    uint32_t pit_freq_hz = (PIT_DEFAULT_DIVISOR == 0) ? (PIT_BASE_FREQUENCY / 65536) : (PIT_BASE_FREQUENCY / PIT_DEFAULT_DIVISOR);
    
    // Calculate remaining time
    if (system_ticks >= shutdown_scheduled_tick) {
        // Time to shutdown!
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        kprint("");
        kprint("System is going down for poweroff NOW!");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        acpi_shutdown();
        return;
    }
    
    // Print countdown messages at 60s, 30s, 10s, 5s, 4s, 3s, 2s, 1s
    uint32_t remaining_ticks = shutdown_scheduled_tick - system_ticks;
    uint32_t remaining_seconds = remaining_ticks / pit_freq_hz;
    
    // Only print message once per second
    uint32_t current_second = system_ticks / pit_freq_hz;
    uint32_t last_message_second = shutdown_message_last_tick / pit_freq_hz;
    
    if (current_second != last_message_second) {
        if (remaining_seconds == 60 || remaining_seconds == 30 || remaining_seconds == 10 ||
            remaining_seconds == 5 || remaining_seconds == 4 || remaining_seconds == 3 ||
            remaining_seconds == 2 || remaining_seconds == 1) {
            vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
            vga_puts("\nShutdown in ");
            char buf[16];
            itoa(remaining_seconds, buf, 10);
            vga_puts(buf);
            vga_puts(" second");
            if (remaining_seconds != 1) vga_puts("s");
            vga_puts("...");
            vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            kprint("");
            shell_display_prompt();
            // Redisplay current input
            for (uint32_t i = 0; i < input_pos; i++) {
                vga_putc(input_buffer[i]);
            }
            shutdown_message_last_tick = system_ticks;
        }
    }
}
