/*
 * === AOS HEADER BEGIN ===
 * src/kernel/syscall.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <syscall.h>
#include <fs/vfs.h>
#include <arch.h>
#include <arch_types.h>
#include <serial.h>
#include <sandbox.h>
#include <process.h>
#include <keyboard.h>
#include <vga.h>
#include <user.h>
#include <string.h>
#include <command_registry.h>
#include <version.h>
#include <crypto/sha256.h>
#include <fs_layout.h>
#include <dev/mouse.h>
#include <acpi.h>
#include <stdlib.h>
#include <limits.h>

// Forward declaration for process functions
extern void process_exit(int status);
extern int process_fork(void);
extern int process_getpid(void);
extern int process_kill(int pid, int signal);
extern int process_waitpid(int pid, int* status, int options);
extern int process_execve(const char* path, char* const argv[], char* const envp[]);
extern void* process_sbrk(int increment);
extern void process_sleep(uint32_t milliseconds);
extern void process_yield(void);
extern volatile uint32_t shutdown_scheduled_tick;
extern volatile uint32_t shutdown_message_last_tick;
extern void kprint(const char *str);

static void syscall_check_scheduled_shutdown(void) {
    if (shutdown_scheduled_tick == 0) {
        return;
    }

    uint32_t now_ticks = arch_timer_get_ticks();
    uint32_t pit_freq_hz = arch_timer_get_frequency();
    if (pit_freq_hz == 0) {
        pit_freq_hz = 100;
    }

    if (now_ticks >= shutdown_scheduled_tick) {
        vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        kprint("");
        kprint("System is going down for poweroff NOW!");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        acpi_shutdown();
        return;
    }

    uint32_t remaining_ticks = shutdown_scheduled_tick - now_ticks;
    uint32_t remaining_seconds = remaining_ticks / pit_freq_hz;
    uint32_t current_second = now_ticks / pit_freq_hz;
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
            shutdown_message_last_tick = now_ticks;
        }
    }
}

// System call table
typedef intptr_t (*syscall_handler_t)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

static int syscall_to_u32(uintptr_t value, uint32_t* out) {
    if (!out || value > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int syscall_to_int(uintptr_t value, int* out) {
    if (!out) {
        return -1;
    }
    intptr_t signed_value = (intptr_t)value;
    if (signed_value < INT_MIN || signed_value > INT_MAX) {
        return -1;
    }
    *out = (int)signed_value;
    return 0;
}

static intptr_t syscall_exit(uintptr_t status, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    process_exit((int)status);
    return 0;  // Never reached
}

static intptr_t syscall_fork(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return process_fork();
}

static intptr_t syscall_read(uintptr_t fd, uintptr_t buffer, uintptr_t size, uintptr_t d, uintptr_t e) {
    (void)d; (void)e;
    int fd_value = 0;
    uint32_t size_value = 0;
    if (syscall_to_int(fd, &fd_value) != 0 || syscall_to_u32(size, &size_value) != 0) {
        return -1;
    }
    return vfs_read(fd_value, (void*)buffer, size_value);
}

static intptr_t syscall_write(uintptr_t fd, uintptr_t buffer, uintptr_t size, uintptr_t d, uintptr_t e) {
    (void)d; (void)e;
    int fd_value = 0;
    uint32_t size_value = 0;
    if (syscall_to_int(fd, &fd_value) != 0 || syscall_to_u32(size, &size_value) != 0) {
        return -1;
    }
    return vfs_write(fd_value, (const void*)buffer, size_value);
}

static intptr_t syscall_open(uintptr_t path, uintptr_t flags, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    uint32_t flags_value = 0;
    if (syscall_to_u32(flags, &flags_value) != 0) {
        return -1;
    }
    return vfs_open((const char*)path, flags_value);
}

static intptr_t syscall_close(uintptr_t fd, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    int fd_value = 0;
    if (syscall_to_int(fd, &fd_value) != 0) {
        return -1;
    }
    return vfs_close(fd_value);
}

static intptr_t syscall_waitpid(uintptr_t pid, uintptr_t status, uintptr_t options, uintptr_t d, uintptr_t e) {
    (void)d; (void)e;
    int pid_value = 0;
    int options_value = 0;
    if (syscall_to_int(pid, &pid_value) != 0 || syscall_to_int(options, &options_value) != 0) {
        return -1;
    }
    return process_waitpid(pid_value, (int*)status, options_value);
}

static intptr_t syscall_execve(uintptr_t path, uintptr_t argv, uintptr_t envp, uintptr_t d, uintptr_t e) {
    (void)d; (void)e;
    return process_execve((const char*)path, (char* const*)argv, (char* const*)envp);
}

static intptr_t syscall_getpid(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return process_getpid();
}

static intptr_t syscall_kill(uintptr_t pid, uintptr_t signal, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    int pid_value = 0;
    int signal_value = 0;
    if (syscall_to_int(pid, &pid_value) != 0 || syscall_to_int(signal, &signal_value) != 0) {
        return -1;
    }
    return process_kill(pid_value, signal_value);
}

static intptr_t syscall_lseek(uintptr_t fd, uintptr_t offset, uintptr_t whence, uintptr_t d, uintptr_t e) {
    (void)d; (void)e;
    int fd_value = 0;
    int offset_value = 0;
    int whence_value = 0;
    if (syscall_to_int(fd, &fd_value) != 0 ||
        syscall_to_int(offset, &offset_value) != 0 ||
        syscall_to_int(whence, &whence_value) != 0) {
        return -1;
    }
    return vfs_lseek(fd_value, offset_value, whence_value);
}

static intptr_t syscall_readdir(uintptr_t fd, uintptr_t dirent, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    int fd_value = 0;
    if (syscall_to_int(fd, &fd_value) != 0) {
        return -1;
    }
    return vfs_readdir(fd_value, (dirent_t*)dirent);
}

static intptr_t syscall_mkdir(uintptr_t path, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_mkdir((const char*)path);
}

static intptr_t syscall_rmdir(uintptr_t path, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_rmdir((const char*)path);
}

static intptr_t syscall_unlink(uintptr_t path, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_unlink((const char*)path);
}

static intptr_t syscall_stat(uintptr_t path, uintptr_t stat, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    return vfs_stat((const char*)path, (stat_t*)stat);
}

static intptr_t syscall_sbrk(uintptr_t increment, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    int increment_value = 0;
    if (syscall_to_int(increment, &increment_value) != 0) {
        return -1;
    }
    return (intptr_t)process_sbrk(increment_value);
}

static intptr_t syscall_sleep(uintptr_t milliseconds, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    uint32_t ms_value = 0;
    if (syscall_to_u32(milliseconds, &ms_value) != 0) {
        return -1;
    }
    process_sleep(ms_value);
    return 0;
}

static intptr_t syscall_yield(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    process_yield();
    return 0;
}

// === Ring 3 Shell Syscalls ===

static intptr_t syscall_putchar(uintptr_t ch, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    vga_putc((char)ch);
    return 0;
}

static intptr_t syscall_getchar(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    // INT 0x80 entry runs with IF cleared in this kernel; re-enable IRQs here
    // so PIT timekeeping continues while waiting for user input.
    __asm__ volatile ("sti");

    // Blocking keyboard read.
    while (1) {
        syscall_check_scheduled_shutdown();

        // Poll mouse for scroll wheel events while waiting for keyboard input
        mouse_poll();
        if (mouse_has_data()) {
            mouse_packet_t* packet = mouse_get_packet();
            if (packet && packet->z_movement != 0) {
                if (packet->z_movement > 0) {
                    vga_scroll_up_view();
                } else {
                    vga_scroll_down();
                }
            }
        }
        
        uint8_t scancode = keyboard_get_scancode();
        if (scancode != 0) {
            char ch = scancode_to_char(scancode);
            if (ch != 0) {
                int result = (unsigned char)ch;
                if (keyboard_is_ctrl_pressed())  result |= (1 << 8);
                if (keyboard_is_shift_pressed()) result |= (1 << 9);
                if (keyboard_is_alt_pressed())   result |= (1 << 10);
                return result;
            }
        }
        // Wait for next interrupt instead of burning CPU in a tight loop.
        __asm__ volatile ("hlt");
    }
}

static intptr_t syscall_kcmd(uintptr_t cmd_ptr, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    const char* cmd = (const char*)cmd_ptr;
    if (!cmd || !*cmd) return -1;
    return execute_command(cmd);
}

static intptr_t syscall_getcwd(uintptr_t buf_ptr, uintptr_t len, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    size_t max_len = (size_t)len;
    if (!buf || max_len == 0) return -1;
    const char* cwd = vfs_getcwd();
    if (!cwd) { buf[0] = '/'; buf[1] = '\0'; return 1; }
    size_t slen = strlen(cwd);
    if (slen >= max_len) slen = max_len - 1;
    memcpy(buf, cwd, slen);
    buf[slen] = '\0';
    return (intptr_t)slen;
}

static intptr_t syscall_setcolor(uintptr_t color, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    vga_set_color((uint8_t)color);
    return 0;
}

static intptr_t syscall_clear(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_clear_all();
    return 0;
}

static intptr_t syscall_getuser(uintptr_t buf_ptr, uintptr_t len, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    size_t max_len = (size_t)len;
    if (!buf || max_len == 0) return -1;
    session_t* session = user_get_session();
    if (!session || !session->user) {
        // No user logged in - return "?" as placeholder
        buf[0] = '?'; buf[1] = '\0';
        return 1;
    }
    size_t slen = strlen(session->user->username);
    if (slen >= max_len) slen = max_len - 1;
    memcpy(buf, session->user->username, slen);
    buf[slen] = '\0';
    return (intptr_t)slen;
}

static intptr_t syscall_isroot(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return user_is_root();
}

static intptr_t syscall_login(uintptr_t user_ptr, uintptr_t pass_ptr, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    const char* username = (const char*)user_ptr;
    const char* password = (const char*)pass_ptr;
    if (!username || !password) return -1;

    user_t* user = user_authenticate(username, password);
    if (!user) return -1;

    // Set up session
    session_t* session = user_get_session();
    if (session) {
        session->user = user;
        session->session_flags = SESSION_FLAG_LOGGED_IN;
        if (user->uid == 0) session->session_flags |= SESSION_FLAG_ROOT;
    }

    // Change to home directory
    if (user->home_dir[0]) {
        vfs_chdir(user->home_dir);
    }

    // Enable cursor
    vga_enable_cursor();

    return 0;
}

static intptr_t syscall_logout(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    user_logout();
    return 0;
}

static intptr_t syscall_getversion(uintptr_t buf_ptr, uintptr_t len, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    size_t max_len = (size_t)len;
    if (!buf || max_len == 0) return -1;
    
    const char* version = AOS_VERSION_SHORT;
    size_t vlen = strlen(version);
    if (vlen >= max_len) vlen = max_len - 1;
    memcpy(buf, version, vlen);
    buf[vlen] = '\0';
    return (intptr_t)vlen;
}

static intptr_t syscall_isfirsttime(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    user_t* root = user_find_by_name("root");
    if (!root) return 0;
    
    // Check if root still has default password "root"
    char default_hash[MAX_PASSWORD_HASH];
    sha256_ctx_t ctx;
    uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t*)"root", 4);
    sha256_final(&ctx, digest);
    sha256_to_hex(digest, default_hash);
    
    return (strcmp(root->password_hash, default_hash) == 0) ? 1 : 0;
}

static intptr_t syscall_getuserflags(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    session_t* session = user_get_session();
    if (!session || !session->user) return 0;
    return (int)session->user->flags;
}

static intptr_t syscall_setpassword(uintptr_t user_ptr, uintptr_t pass_ptr, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    const char* username = (const char*)user_ptr;
    const char* password = (const char*)pass_ptr;
    if (!username || !password) return -1;
    
    int result = user_set_password(username, password);
    if (result == 0) {
        // Clear must-change-password flag if set
        user_t* user = user_find_by_name(username);
        if (user) {
            user->flags &= ~USER_FLAG_MUST_CHANGE_PASS;
        }
        
        // Save user database if in LOCAL mode
        if (fs_layout_get_mode() == FS_MODE_LOCAL) {
            if (user_save_database(USER_DATABASE_PATH) == 0) {
                serial_puts("User database saved after password change\n");
            } else {
                serial_puts("Warning: Failed to save user database\n");
            }
        }
    }
    return result;
}

static intptr_t syscall_getunformatted(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    extern int unformatted_disk_detected;
    int val = unformatted_disk_detected;
    unformatted_disk_detected = 0;  // Clear after reading
    return val;
}

static intptr_t syscall_gethomedir(uintptr_t buf_ptr, uintptr_t len, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    size_t max_len = (size_t)len;
    if (!buf || max_len == 0) return -1;
    
    session_t* session = user_get_session();
    if (!session || !session->user) return -1;
    
    size_t hlen = strlen(session->user->home_dir);
    if (hlen >= max_len) hlen = max_len - 1;
    memcpy(buf, session->user->home_dir, hlen);
    buf[hlen] = '\0';
    return (intptr_t)hlen;
}

static intptr_t syscall_vga_enable_cursor(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_enable_cursor();
    return 0;
}

static intptr_t syscall_vga_disable_cursor(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_disable_cursor();
    return 0;
}

static intptr_t syscall_vga_set_cursor_style(uintptr_t style, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    vga_set_cursor_style((vga_cursor_style_t)style);
    return 0;
}

static intptr_t syscall_vga_get_pos(uintptr_t row_ptr, uintptr_t col_ptr, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    uint8_t* row = (uint8_t*)row_ptr;
    uint8_t* col = (uint8_t*)col_ptr;
    if (row) *row = vga_get_row();
    if (col) *col = vga_get_col();
    return 0;
}

static intptr_t syscall_vga_set_pos(uintptr_t row, uintptr_t col, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)c; (void)d; (void)e;
    vga_set_position((uint8_t)row, (uint8_t)col);
    return 0;
}

static intptr_t syscall_vga_backspace(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_backspace();
    return 0;
}

static intptr_t syscall_vga_scroll_up_view(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_scroll_up_view();
    return 0;
}

static intptr_t syscall_vga_scroll_down(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_scroll_down();
    return 0;
}

static intptr_t syscall_vga_scroll_to_bottom(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_scroll_to_bottom();
    return 0;
}

static intptr_t syscall_mouse_poll(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    mouse_poll();
    return 0;
}

static intptr_t syscall_mouse_has_data(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return mouse_has_data();
}

static intptr_t syscall_mouse_get_packet(uintptr_t packet_ptr, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e) {
    (void)b; (void)c; (void)d; (void)e;
    mouse_packet_t* user_packet = (mouse_packet_t*)packet_ptr;
    if (!user_packet) return -1;
    
    mouse_packet_t* kernel_packet = mouse_get_packet();
    if (!kernel_packet) return 0;
    
    // Copy packet data to userspace
    user_packet->buttons = kernel_packet->buttons;
    user_packet->x_movement = kernel_packet->x_movement;
    user_packet->y_movement = kernel_packet->y_movement;
    user_packet->z_movement = kernel_packet->z_movement;
    return 1;
}

// System call table — indices MUST match SYS_* defines in syscall.h
static syscall_handler_t syscall_table[SYSCALL_COUNT] = {
    [SYS_EXIT]      = syscall_exit,
    [SYS_FORK]      = syscall_fork,
    [SYS_READ]      = syscall_read,
    [SYS_WRITE]     = syscall_write,
    [SYS_OPEN]      = syscall_open,
    [SYS_CLOSE]     = syscall_close,
    [SYS_WAITPID]   = syscall_waitpid,
    [SYS_EXECVE]    = syscall_execve,
    [SYS_GETPID]    = syscall_getpid,
    [SYS_KILL]      = syscall_kill,
    [SYS_LSEEK]     = syscall_lseek,
    [SYS_READDIR]   = syscall_readdir,
    [SYS_MKDIR]     = syscall_mkdir,
    [SYS_RMDIR]     = syscall_rmdir,
    [SYS_UNLINK]    = syscall_unlink,
    [SYS_STAT]      = syscall_stat,
    [SYS_SBRK]      = syscall_sbrk,
    [SYS_SLEEP]     = syscall_sleep,
    [SYS_YIELD]     = syscall_yield,
    [SYS_PUTCHAR]   = syscall_putchar,
    [SYS_GETCHAR]   = syscall_getchar,
    [SYS_KCMD]      = syscall_kcmd,
    [SYS_GETCWD]    = syscall_getcwd,
    [SYS_SETCOLOR]  = syscall_setcolor,
    [SYS_CLEAR]     = syscall_clear,
    [SYS_GETUSER]   = syscall_getuser,
    [SYS_ISROOT]    = syscall_isroot,
    [SYS_LOGIN]     = syscall_login,
    [SYS_LOGOUT]    = syscall_logout,
    [SYS_GETVERSION] = syscall_getversion,
    [SYS_ISFIRSTTIME] = syscall_isfirsttime,
    [SYS_GETUSERFLAGS] = syscall_getuserflags,
    [SYS_SETPASSWORD] = syscall_setpassword,
    [SYS_GETUNFORMATTED] = syscall_getunformatted,
    [SYS_GETHOMEDIR] = syscall_gethomedir,
    [SYS_VGA_ENABLE_CURSOR] = syscall_vga_enable_cursor,
    [SYS_VGA_DISABLE_CURSOR] = syscall_vga_disable_cursor,
    [SYS_VGA_SET_CURSOR_STYLE] = syscall_vga_set_cursor_style,
    [SYS_VGA_GET_POS] = syscall_vga_get_pos,
    [SYS_VGA_SET_POS] = syscall_vga_set_pos,
    [SYS_VGA_BACKSPACE] = syscall_vga_backspace,
    [SYS_VGA_SCROLL_UP_VIEW] = syscall_vga_scroll_up_view,
    [SYS_VGA_SCROLL_DOWN] = syscall_vga_scroll_down,
    [SYS_VGA_SCROLL_TO_BOTTOM] = syscall_vga_scroll_to_bottom,
    [SYS_MOUSE_POLL] = syscall_mouse_poll,
    [SYS_MOUSE_HAS_DATA] = syscall_mouse_has_data,
    [SYS_MOUSE_GET_PACKET] = syscall_mouse_get_packet,
};

// System call interrupt handler (INT 0x80)
void syscall_handler(void* regs_ptr) {
    arch_registers_t* regs = (arch_registers_t*)regs_ptr;
    
    // System call number in EAX, arguments in EBX, ECX, EDX, ESI, EDI
    uintptr_t syscall_num = (uintptr_t)regs->eax;
    intptr_t result = -1;

    // Prevent scheduler-driven context switches while executing syscall code.
    process_set_preempt_disabled(1);
    
    if (syscall_num >= SYSCALL_COUNT) {
        serial_puts("Invalid syscall number: ");
        goto out;
    }
    
    // Get current process and check sandbox permissions (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        // Check if syscall is allowed by sandbox filter
        if (!syscall_check_allowed((uint32_t)syscall_num, proc->sandbox.syscall_filter)) {
            serial_puts("Syscall blocked by sandbox: tid=");
            char pid_buf[16];
            itoa(proc->pid, pid_buf, 10);
            serial_puts(pid_buf);
            serial_puts(" name=");
            serial_puts(proc->name);
            serial_puts(" syscall=");
            char sys_buf[16];
            itoa((int)syscall_num, sys_buf, 10);
            serial_puts(sys_buf);
            serial_puts(" filter=0x");
            char hex[9];
            uint32_t f = proc->sandbox.syscall_filter;
            for (int i = 7; i >= 0; i--) {
                int nibble = (f >> (i * 4)) & 0xF;
                hex[7 - i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
            }
            hex[8] = '\0';
            serial_puts(hex);
            serial_puts("\n");
            goto out;
        }
        
        // Check CPU time limit
        if (!resource_check_time(proc->pid)) {
            serial_puts("Process exceeded CPU time limit\n");
            process_exit(-1);
            goto out;
        }
    }
    
    // Call the appropriate handler
    syscall_handler_t handler = syscall_table[(size_t)syscall_num];
    if (handler) {
        result = handler((uintptr_t)regs->ebx,
                         (uintptr_t)regs->ecx,
                         (uintptr_t)regs->edx,
                         (uintptr_t)regs->esi,
                         (uintptr_t)regs->edi);
    }
    
out:
    process_set_preempt_disabled(0);
    regs->eax = (uintptr_t)result;
}

// Initialize system call handler
void init_syscalls(void) {
    serial_puts("Initializing system call interface (INT 0x80)...\n");
    
    // Register INT 0x80 handler
    arch_register_interrupt_handler(0x80, syscall_handler);
    
    serial_puts("System call interface initialized.\n");
}

// ===== Kernel-mode wrapper functions =====
// These are for kernel code to call VFS operations directly
// User-mode code should use INT 0x80 with the syscall numbers

int sys_open(const char* path, uint32_t flags) {
    return vfs_open(path, flags);
}

int sys_close(int fd) {
    return vfs_close(fd);
}

int sys_read(int fd, void* buffer, uint32_t size) {
    return vfs_read(fd, buffer, size);
}

int sys_write(int fd, const void* buffer, uint32_t size) {
    return vfs_write(fd, buffer, size);
}

int sys_lseek(int fd, int offset, int whence) {
    return vfs_lseek(fd, offset, whence);
}

int sys_readdir(int fd, dirent_t* dirent) {
    return vfs_readdir(fd, dirent);
}

int sys_mkdir(const char* path) {
    return vfs_mkdir(path);
}

int sys_rmdir(const char* path) {
    return vfs_rmdir(path);
}

int sys_unlink(const char* path) {
    return vfs_unlink(path);
}

int sys_stat(const char* path, stat_t* stat) {
    return vfs_stat(path, stat);
}

void* sys_sbrk(int increment) {
    return process_sbrk(increment);
}
