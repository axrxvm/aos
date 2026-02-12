/*
 * === AOS HEADER BEGIN ===
 * ubin/aosh.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/*
 * aosh - aOS Shell (Ring 3 Userspace Shell)
 *
 * Runs entirely in CPU ring 3. All kernel interactions go through INT 0x80 syscalls.
 * Compiled as a standalone flat binary at 0x08048000.
 */

 /* SYSCALL NUMBERS
 * (must match kernel include/syscall.h)
 */

#define SYS_EXIT        0
#define SYS_READ        2
#define SYS_WRITE       3
#define SYS_OPEN        4
#define SYS_CLOSE       5
#define SYS_PUTCHAR     19
#define SYS_GETCHAR     20
#define SYS_KCMD        21
#define SYS_GETCWD      22
#define SYS_SETCOLOR    23
#define SYS_CLEAR       24
#define SYS_GETUSER     25
#define SYS_ISROOT      26
#define SYS_LOGIN       27
#define SYS_LOGOUT      28
#define SYS_GETVERSION  29
#define SYS_ISFIRSTTIME 30
#define SYS_GETUSERFLAGS 31
#define SYS_SETPASSWORD 32
#define SYS_GETUNFORMATTED 33
#define SYS_GETHOMEDIR  34
#define SYS_VGA_ENABLE_CURSOR 35
#define SYS_VGA_DISABLE_CURSOR 36
#define SYS_VGA_SET_CURSOR_STYLE 37
#define SYS_VGA_GET_POS 38
#define SYS_VGA_SET_POS 39
#define SYS_VGA_BACKSPACE 40
#define SYS_VGA_SCROLL_UP_VIEW 41
#define SYS_VGA_SCROLL_DOWN 42
#define SYS_VGA_SCROLL_TO_BOTTOM 43
#define SYS_MOUSE_POLL 44
#define SYS_MOUSE_HAS_DATA 45
#define SYS_MOUSE_GET_PACKET 46

/* File operation flags (from vfs.h) */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200

/* Special key codes (must match kernel keyboard.h) */
#define KEY_UP      0x1E
#define KEY_DOWN    0x1F
#define KEY_LEFT    0x1A
#define KEY_RIGHT   0x1B

/* Cursor styles */
#define CURSOR_BLOCK 0
#define CURSOR_UNDERLINE 1
#define CURSOR_BLINK 2

/* User flags (must match kernel include/user.h) */
#define USER_FLAG_MUST_CHANGE_PASS 0x10

/* Mouse packet structure (must match kernel include/dev/mouse.h) */
typedef struct {
    unsigned char buttons;
    signed char x_movement;
    signed char y_movement;
    signed char z_movement;
} mouse_packet_t;

/*
 * SYSCALL WRAPPERS
*/

static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}

/*
 *  KERNEL INTERFACE
*/

static void u_exit(int status) {
    syscall1(SYS_EXIT, status);
    for (;;) __asm__ volatile ("hlt");  /* never reached */
}

static void u_putchar(int c) {
    syscall1(SYS_PUTCHAR, c);
}

static int u_getchar(void) {
    return syscall0(SYS_GETCHAR);
}

static int u_kcmd(const char* cmd) {
    return syscall1(SYS_KCMD, (int)cmd);
}

static int u_getcwd(char* buf, int len) {
    return syscall2(SYS_GETCWD, (int)buf, len);
}

static void u_setcolor(int color) {
    syscall1(SYS_SETCOLOR, color);
}

static void u_clear(void) {
    syscall0(SYS_CLEAR);
}

static int u_getuser(char* buf, int len) {
    return syscall2(SYS_GETUSER, (int)buf, len);
}

static int u_isroot(void) {
    return syscall0(SYS_ISROOT);
}

static int u_login(const char* user, const char* pass) {
    return syscall2(SYS_LOGIN, (int)user, (int)pass);
}

static void u_logout(void) {
    syscall0(SYS_LOGOUT);
}

static int u_getversion(char* buf, int len) {
    return syscall2(SYS_GETVERSION, (int)buf, len);
}

static int u_isfirsttime(void) {
    return syscall0(SYS_ISFIRSTTIME);
}

static int u_getuserflags(void) {
    return syscall0(SYS_GETUSERFLAGS);
}

static int u_setpassword(const char* user, const char* pass) {
    return syscall2(SYS_SETPASSWORD, (int)user, (int)pass);
}

static int u_getunformatted(void) {
    return syscall0(SYS_GETUNFORMATTED);
}

static int u_gethomedir(char* buf, int len) {
    return syscall2(SYS_GETHOMEDIR, (int)buf, len);
}

static void u_vga_enable_cursor(void) {
    syscall0(SYS_VGA_ENABLE_CURSOR);
}

static void u_vga_disable_cursor(void) {
    syscall0(SYS_VGA_DISABLE_CURSOR);
}

static void u_vga_set_cursor_style(int style) {
    syscall1(SYS_VGA_SET_CURSOR_STYLE, style);
}

static void u_vga_get_pos(unsigned char* row, unsigned char* col) {
    syscall2(SYS_VGA_GET_POS, (int)row, (int)col);
}

static void u_vga_set_pos(unsigned char row, unsigned char col) {
    syscall2(SYS_VGA_SET_POS, row, col);
}

static void u_vga_backspace(void) {
    syscall0(SYS_VGA_BACKSPACE);
}

static void u_vga_scroll_up_view(void) {
    syscall0(SYS_VGA_SCROLL_UP_VIEW);
}

static void u_vga_scroll_down(void) {
    syscall0(SYS_VGA_SCROLL_DOWN);
}

static void u_vga_scroll_to_bottom(void) {
    syscall0(SYS_VGA_SCROLL_TO_BOTTOM);
}

static void u_mouse_poll(void) {
    syscall0(SYS_MOUSE_POLL);
}

static int u_mouse_has_data(void) {
    return syscall0(SYS_MOUSE_HAS_DATA);
}

static int u_mouse_get_packet(mouse_packet_t* packet) {
    return syscall1(SYS_MOUSE_GET_PACKET, (int)packet);
}
/* File operations */
static int u_open(const char* path, int flags) {
    return (int)syscall2(SYS_OPEN, (int)path, flags);
}

static int u_close(int fd) {
    return (int)syscall1(SYS_CLOSE, fd);
}

static int u_read(int fd, void* buf, int size) {
    return (int)syscall3(SYS_READ, fd, (int)buf, size);
}

static int u_write(int fd, const void* buf, int size) {
    return (int)syscall3(SYS_WRITE, fd, (int)buf, size);
}
/*
 * STRING UTILITIES
*/

static int u_strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int u_strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] || !b[i] || a[i] != b[i]) {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

static int u_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void u_memset(void* dst, int val, int len) {
    char* d = (char*)dst;
    for (int i = 0; i < len; i++) d[i] = (char)val;
}

static void u_memcpy(void* dst, const void* src, int len) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (int i = 0; i < len; i++) d[i] = s[i];
}

/*
 * I/O HELPERS
*/

static void u_puts(const char* s) {
    while (*s) u_putchar(*s++);
}
static void u_print_line(int len, char ch) {
    for (int i = 0; i < len; i++) u_putchar(ch);
    u_putchar('\n');
}

/*
 * COMMAND HISTORY
*/

#define HISTORY_MAX 50
#define INPUT_MAX 256
#define HISTORY_FILE ".shhistory"

static char history[HISTORY_MAX][INPUT_MAX];
static int history_count = 0;

static void save_history(void) {
    char home[128];
    u_gethomedir(home, 128);
    
    /* Build path: home/.shhistory */
    char path[256];
    u_memset(path, 0, 256);
    u_memcpy(path, home, u_strlen(home));
    
    int len = u_strlen(path);
    if (len > 0 && path[len - 1] != '/') {
        path[len] = '/';
        len++;
    }
    
    const char* filename = HISTORY_FILE;
    for (int i = 0; filename[i]; i++) {
        path[len++] = filename[i];
    }
    path[len] = '\0';
    
    /* Build content - write in chunks to avoid huge stack buffer */
    int fd = u_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return;
    }
    
    for (int i = 0; i < history_count; i++) {
        int line_len = u_strlen(history[i]);
        if (line_len > 0) {
            u_write(fd, history[i], line_len);
            u_write(fd, "\n", 1);
        }
    }
    
    u_close(fd);
}


static void load_history(void) {
    char home[128];
    u_gethomedir(home, 128);
    
    /* Build path: home/.shhistory */
    char path[256];
    u_memset(path, 0, 256);
    u_memcpy(path, home, u_strlen(home));
    
    int len = u_strlen(path);
    if (len > 0 && path[len - 1] != '/') {
        path[len] = '/';
        len++;
    }
    
    const char* filename = HISTORY_FILE;
    for (int i = 0; filename[i]; i++) {
        path[len++] = filename[i];
    }
    path[len] = '\0';
    
    /* Reset history */
    history_count = 0;
    u_memset(history, 0, sizeof(history));
    
    /* Try to open file */
    int fd = u_open(path, O_RDONLY);
    if (fd < 0) {
        return;  /* File doesn't exist, that's okay */
    }
    
    /* Read file - use smaller 4KB buffer */
    char file_buf[4096];
    int bytes_read = u_read(fd, file_buf, sizeof(file_buf) - 1);
    u_close(fd);
    
    if (bytes_read <= 0) {
        return;
    }
    
    if (bytes_read >= (int)sizeof(file_buf)) {
        bytes_read = sizeof(file_buf) - 1;
    }
    file_buf[bytes_read] = '\0';
    
    /* Parse lines */
    char* line_start = file_buf;
    for (int i = 0; i < bytes_read && history_count < HISTORY_MAX; i++) {
        if (file_buf[i] == '\n') {
            file_buf[i] = '\0';
            int line_len = u_strlen(line_start);
            if (line_len > 0 && line_len < INPUT_MAX) {
                u_memcpy(history[history_count], line_start, line_len);
                history[history_count][line_len] = '\0';
                history_count++;
            }
            line_start = &file_buf[i + 1];
        }
    }
    
    /* Handle last line if no trailing newline */
    int last_len = u_strlen(line_start);
    if (last_len > 0 && last_len < INPUT_MAX && history_count < HISTORY_MAX) {
        u_memcpy(history[history_count], line_start, last_len);
        history[history_count][last_len] = '\0';
        history_count++;
    }
}

static void add_to_history(const char* cmd) {
    if (!cmd || !cmd[0]) return;
    
    /* Don't add duplicates of the last command */
    if (history_count > 0 && u_strcmp(history[history_count - 1], cmd) == 0) {
        return;
    }
    
    /* Shift history if full */
    if (history_count >= HISTORY_MAX) {
        for (int i = 0; i < HISTORY_MAX - 1; i++) {
            u_memcpy(history[i], history[i + 1], INPUT_MAX);
        }
        history_count = HISTORY_MAX - 1;
    }
    
    /* Add new command */
    u_memcpy(history[history_count], cmd, INPUT_MAX);
    history_count++;
    
    /* Save to disk */
    save_history();
}

/*
 * LOGIN
*/

static int read_input(char* buf, int maxlen, int mask) {
    int pos = 0;
    u_memset(buf, 0, maxlen);

    while (pos < maxlen - 1) {
        int ch = u_getchar();
        char c = (char)(ch & 0xFF);
        int ctrl = (ch >> 8) & 1;

        /* Ctrl+C cancels */
        if (ctrl && (c == 'c' || c == 'C')) {
            u_puts("^C\n");
            buf[0] = '\0';
            return -1;
        }

        /* Enter */
        if (c == '\n') {
            u_putchar('\n');
            buf[pos] = '\0';
            return pos;
        }

        /* Backspace */
        if (c == '\b') {
            if (pos > 0) {
                pos--;
                u_vga_backspace();
            }
            continue;
        }

        /* Printable */
        if (c >= 0x20 && c < 0x7F) {
            buf[pos++] = c;
            u_putchar(mask ? '*' : c);
        }
    }

    buf[pos] = '\0';
    return pos;
}

/* Enhanced input with arrow key support for command loop */
static int read_input_ex(char* buf, int maxlen) {
    int pos = 0;
    int cursor_pos = 0;
    unsigned char start_row, start_col;
    int history_pos = -1;  /* -1 = not navigating */
    char backup[INPUT_MAX];
    
    u_memset(buf, 0, maxlen);
    u_memset(backup, 0, INPUT_MAX);
    u_vga_get_pos(&start_row, &start_col);
    u_vga_enable_cursor();
    u_vga_set_cursor_style(CURSOR_UNDERLINE);

    while (pos < maxlen - 1) {
        /* Poll mouse for scroll wheel events */
        u_mouse_poll();
        if (u_mouse_has_data()) {
            mouse_packet_t packet;
            if (u_mouse_get_packet(&packet)) {
                if (packet.z_movement > 0) {
                    u_vga_scroll_up_view();
                } else if (packet.z_movement < 0) {
                    u_vga_scroll_down();
                }
            }
            continue; /* Check for more input */
        }
        
        int ch = u_getchar();
        char c = (char)(ch & 0xFF);
        int ctrl = (ch >> 8) & 1;

        /* Ctrl+C cancels */
        if (ctrl && (c == 'c' || c == 'C')) {
            u_puts("^C\n");
            buf[0] = '\0';
            return -1;
        }

        /* Arrow UP - previous command */
        if (c == KEY_UP) {
            if (history_count == 0) continue;
            
            /* First time - save current input */
            if (history_pos == -1) {
                u_memcpy(backup, buf, INPUT_MAX);
                history_pos = history_count - 1;
            } else if (history_pos > 0) {
                history_pos--;
            } else {
                continue;  /* Already at oldest */
            }
            
            /* Clear current line */
            u_vga_set_pos(start_row, start_col);
            for (int i = 0; i < pos; i++) u_putchar(' ');
            u_vga_set_pos(start_row, start_col);
            
            /* Load history */
            u_memcpy(buf, history[history_pos], INPUT_MAX);
            pos = u_strlen(buf);
            cursor_pos = pos;
            u_puts(buf);
            continue;
        }

        /* Arrow DOWN - next command */
        if (c == KEY_DOWN) {
            if (history_pos == -1) continue;  /* Not navigating */
            
            /* Clear current line */
            u_vga_set_pos(start_row, start_col);
            for (int i = 0; i < pos; i++) u_putchar(' ');
            u_vga_set_pos(start_row, start_col);
            
            if (history_pos < history_count - 1) {
                history_pos++;
                u_memcpy(buf, history[history_pos], INPUT_MAX);
            } else {
                /* Back to current input */
                history_pos = -1;
                u_memcpy(buf, backup, INPUT_MAX);
            }
            
            pos = u_strlen(buf);
            cursor_pos = pos;
            u_puts(buf);
            continue;
        }

        /* Arrow LEFT - move cursor left */
        if (c == KEY_LEFT) {
            if (cursor_pos > 0) {
                cursor_pos--;
                u_vga_set_pos(start_row, start_col + cursor_pos);
            }
            continue;
        }

        /* Arrow RIGHT - move cursor right */
        if (c == KEY_RIGHT) {
            if (cursor_pos < pos) {
                cursor_pos++;
                u_vga_set_pos(start_row, start_col + cursor_pos);
            }
            continue;
        }

        /* Enter */
        if (c == '\n') {
            u_putchar('\n');
            buf[pos] = '\0';
            return pos;
        }

        /* Backspace */
        if (c == '\b') {
            if (cursor_pos > 0 && pos > 0) {
                /* Delete character at cursor-1 */
                for (int i = cursor_pos - 1; i < pos - 1; i++) {
                    buf[i] = buf[i + 1];
                }
                pos--;
                cursor_pos--;
                buf[pos] = '\0';
                
                /* Redraw line */
                u_vga_set_pos(start_row, start_col);
                for (int i = 0; i < pos; i++) u_putchar(buf[i]);
                u_putchar(' ');  /* Erase last char */
                u_vga_set_pos(start_row, start_col + cursor_pos);
            }
            continue;
        }

        /* Printable */
        if (c >= 0x20 && c < 0x7F) {
            /* Insert at cursor */
            if (pos < maxlen - 1) {
                /* Shift chars right */
                for (int i = pos; i > cursor_pos; i--) {
                    buf[i] = buf[i - 1];
                }
                buf[cursor_pos] = c;
                pos++;
                cursor_pos++;
                buf[pos] = '\0';
                
                /* Redraw from cursor position */
                u_vga_set_pos(start_row, start_col);
                u_puts(buf);
                u_vga_set_pos(start_row, start_col + cursor_pos);
            }
        }
    }

    buf[pos] = '\0';
    return pos;
}

static int do_password_change(const char* username, int is_first_time) {
    char newpass[64];
    char confirm[64];
    
    while (1) {
        u_setcolor(0x0A);  /* green */
        u_puts("New password: ");
        u_setcolor(0x0F);  /* white */
        
        if (read_input(newpass, 64, 1) < 0) {
            u_memset(newpass, 0, 64);
            return -1;  /* Ctrl+C */
        }
        
        if (u_strlen(newpass) < 4) {
            u_setcolor(0x0C);  /* red */
            u_puts("\nPassword too short (minimum 4 characters).\n");
            if (is_first_time) {
                u_puts("Keeping default password. Change it later.\n\n");
                u_setcolor(0x0F);
                u_memset(newpass, 0, 64);
                return 0;  /* Skip but continue */
            }
            u_puts("Try again.\n\n");
            u_setcolor(0x0F);
            continue;
        }
        
        u_setcolor(0x0A);  /* green */
        u_puts("Confirm password: ");
        u_setcolor(0x0F);  /* white */
        
        if (read_input(confirm, 64, 1) < 0) {
            u_memset(newpass, 0, 64);
            u_memset(confirm, 0, 64);
            return -1;  /* Ctrl+C */
        }
        
        if (u_strcmp(newpass, confirm) != 0) {
            u_setcolor(0x0C);  /* red */
            u_puts("\nPasswords do not match.\n");
            if (is_first_time) {
                u_puts("Keeping default password. Change it later.\n\n");
                u_setcolor(0x0F);
                u_memset(newpass, 0, 64);
                u_memset(confirm, 0, 64);
                return 0;  /* Skip but continue */
            }
            u_puts("Try again.\n\n");
            u_setcolor(0x0F);
            u_memset(newpass, 0, 64);
            u_memset(confirm, 0, 64);
            continue;
        }
        
        /* Change password */
        if (u_setpassword(username, newpass) == 0) {
            u_setcolor(0x0A);  /* green */
            u_puts("\nPassword changed successfully!\n\n");
            u_setcolor(0x0F);
            u_memset(newpass, 0, 64);
            u_memset(confirm, 0, 64);
            return 1;  /* Success */
        } else {
            u_setcolor(0x0C);  /* red */
            u_puts("\nFailed to change password. Try again.\n\n");
            u_setcolor(0x0F);
            u_memset(newpass, 0, 64);
            u_memset(confirm, 0, 64);
        }
    }
}

static void do_login(void) {
    char user[64];
    char pass[64];
    int first_time = u_isfirsttime();
    char version[16];
    u_getversion(version, 16);
    
    u_clear();
    
    if (first_time) {
        /* Display red header banner for first-time setup */
        u_setcolor(0x0C);  /* light red */
        u_puts("================================================================================");
        u_putchar('\n');
        u_puts("                              aOS LOGIN SYSTEM                                 ");
        u_putchar('\n');
        u_puts("================================================================================");
        u_putchar('\n');
        u_setcolor(0x0F);  /* white */
        u_putchar('\n');
        
        /* Display version and system info */
        u_setcolor(0x07);  /* light gray */
        u_puts("                         Welcome to aOS v");
        u_puts(version);
        u_putchar('\n');
        u_puts("                    A Modern i386 Operating System\n");
        u_setcolor(0x0F);  /* white */
        u_putchar('\n');
        
        /* First-time setup instructions */
        u_setcolor(0x0E);  /* yellow */
        u_puts("                          FIRST TIME SETUP\n");
        u_puts("                          ================\n\n");
        u_setcolor(0x0F);  /* white */
        u_puts("  Welcome! Please login with the default credentials:\n\n");
        u_setcolor(0x0A);  /* green */
        u_puts("    Username: ");
        u_setcolor(0x0B);  /* cyan */
        u_puts("root\n");
        u_setcolor(0x0A);  /* green */
        u_puts("    Password: ");
        u_setcolor(0x0B);  /* cyan */
        u_puts("root\n\n");
        u_setcolor(0x0F);  /* white */
        u_puts("  You will be prompted to set a new password after login.\n\n");
        u_setcolor(0x08);  /* dark gray */
        u_print_line(80, '-');
        u_setcolor(0x0F);
        u_putchar('\n');
    } else {
        /* Minimal login screen for subsequent logins */
        u_setcolor(0x0F);  /* white */
        u_puts("aos v");
        u_puts(version);
        u_puts(" - aosh login\n");
    }

    while (1) {
        u_setcolor(0x0F);  /* white */
        u_puts("username: ");
        if (read_input(user, 64, 0) < 0) continue;
        if (user[0] == '\0') continue;

        u_puts("password: ");
        if (read_input(pass, 64, 1) < 0) continue;

        if (u_login(user, pass) == 0) {
            /* Login successful */
            u_putchar('\n');
            u_setcolor(0x0A);  /* green */
            u_puts("Login successful! Welcome, ");
            u_setcolor(0x0B);  /* cyan */
            u_puts(user);
            u_setcolor(0x0A);  /* green */
            u_puts("!\n\n");
            u_setcolor(0x0F);  /* white */
            
            /* Check if user must change password */
            int flags = u_getuserflags();
            if ((flags & USER_FLAG_MUST_CHANGE_PASS) && !first_time) {
                u_setcolor(0x0E);  /* yellow */
                u_puts("You must change your password before continuing.\n\n");
                u_setcolor(0x0F);
                if (do_password_change(user, 0) < 0) {
                    u_logout();
                    continue;  /* Ctrl+C during password change */
                }
            }
            
            /* Check if this is first login with default password (root only) */
            if (first_time && u_strcmp(user, "root") == 0) {
                u_setcolor(0x0E);  /* yellow */
                u_puts("Please set a new password for security.\n");
                u_setcolor(0x0F);
                do_password_change(user, 1);
            }
            
            u_memset(pass, 0, 64);
            return;  /* Success */
        }

        u_setcolor(0x0C);  /* red */
        u_puts("\nLogin incorrect.\n\n");
        u_setcolor(0x0F);  /* white */
        
        /* Brief delay */
        for (volatile int i = 0; i < 30000000; i++);
    }
}

/*
 * SHELL PROMPT
*/

static void show_prompt(void) {
    char cwd[256];
    char user[64];
    char home[128];

    u_getuser(user, 64);
    u_getcwd(cwd, 256);
    u_gethomedir(home, 128);

    u_setcolor(0x0A);  /* light green */
    u_putchar('[');
    u_puts(user);
    u_puts("@aOS:");

    u_setcolor(0x0B);  /* cyan */
    
    /* If in home directory, display ~ */
    int home_len = u_strlen(home);
    if (u_strncmp(cwd, home, home_len) == 0) {
        u_putchar('~');
        const char* subdir = cwd + home_len;
        if (*subdir) {
            u_puts(subdir);
        }
    } else {
        u_puts(cwd);
    }

    u_setcolor(0x0A);
    u_putchar(']');

    if (u_isroot()) {
        u_setcolor(0x0C);  /* light red */
        u_puts("# ");
    } else {
        u_setcolor(0x0A);
        u_puts("$ ");
    }

    u_setcolor(0x0F);  /* white */
}

/*
 * ENTRY POINT
*/

__attribute__((section(".text.entry")))
void _start(void) {
    char version[16];
    u_getversion(version, 16);
    
    while (1) {
        /* === Login === */
        u_clear();
        u_setcolor(0x0F);
        do_login();

        /* === Load History === */
        load_history();

        /* === Banner === */
        u_clear();
        
        /* Display ASCII art banner */
        u_setcolor(0x02);  /* green */
        u_puts("         ___  ____  \n");
        u_puts("   __ _ / _ \\/ ___| \n");
        u_puts("  / _` | | | \\___ \\ \n");
        u_puts(" | (_| | |_| |___) |\n");
        u_puts("  \\__,_|\\___/|____/ \n");
        u_puts("                    \n");
        u_setcolor(0x0F);  /* white */
        u_puts("aosh - running on The aOS Kernel\n");
        //u_puts("aos kernel version: ");
        //u_puts(version);
        // u_puts("\n");
        
        /* Display information about unformatted disk if detected */
        if (u_getunformatted()) {
            u_setcolor(0x0E);  /* yellow */
            u_puts("[INFO] Unformatted disk detected!\n");
            u_setcolor(0x0F);  /* white */
            u_puts("To use the disk for persistent storage, run the 'format' command.\n");
            u_puts("Note: This will prepare the disk with the SimpleFS filesystem.\n\n");
        }

        /* === Command Loop === */
        char line[256];

        while (1) {
            show_prompt();

            int n = read_input_ex(line, 256);
            if (n < 0) continue;   /* Ctrl+C */
            if (n == 0) continue;  /* empty */

            /* Add to history */
            add_to_history(line);

            /* --- Shell builtins --- */
            if (u_strcmp(line, "exit") == 0 || u_strcmp(line, "logout") == 0) {
                u_logout();
                u_clear();
                break;  /* back to login */
            }

            /* --- Dispatch to kernel --- */
            u_kcmd(line);
        }
    }
}
