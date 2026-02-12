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

// System call table
typedef int (*syscall_handler_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static int syscall_exit(uint32_t status, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    process_exit((int)status);
    return 0;  // Never reached
}

static int syscall_fork(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return process_fork();
}

static int syscall_read(uint32_t fd, uint32_t buffer, uint32_t size, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return vfs_read((int)fd, (void*)buffer, size);
}

static int syscall_write(uint32_t fd, uint32_t buffer, uint32_t size, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return vfs_write((int)fd, (const void*)buffer, size);
}

static int syscall_open(uint32_t path, uint32_t flags, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return vfs_open((const char*)path, flags);
}

static int syscall_close(uint32_t fd, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_close((int)fd);
}

static int syscall_waitpid(uint32_t pid, uint32_t status, uint32_t options, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return process_waitpid((int)pid, (int*)status, (int)options);
}

static int syscall_execve(uint32_t path, uint32_t argv, uint32_t envp, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return process_execve((const char*)path, (char* const*)argv, (char* const*)envp);
}

static int syscall_getpid(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return process_getpid();
}

static int syscall_kill(uint32_t pid, uint32_t signal, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return process_kill((int)pid, (int)signal);
}

static int syscall_lseek(uint32_t fd, uint32_t offset, uint32_t whence, uint32_t d, uint32_t e) {
    (void)d; (void)e;
    return vfs_lseek((int)fd, (int)offset, (int)whence);
}

static int syscall_readdir(uint32_t fd, uint32_t dirent, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return vfs_readdir((int)fd, (dirent_t*)dirent);
}

static int syscall_mkdir(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_mkdir((const char*)path);
}

static int syscall_rmdir(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_rmdir((const char*)path);
}

static int syscall_unlink(uint32_t path, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return vfs_unlink((const char*)path);
}

static int syscall_stat(uint32_t path, uint32_t stat, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    return vfs_stat((const char*)path, (stat_t*)stat);
}

static int syscall_sbrk(uint32_t increment, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    return (int)process_sbrk((int)increment);
}

static int syscall_sleep(uint32_t milliseconds, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    process_sleep(milliseconds);
    return 0;
}

static int syscall_yield(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    process_yield();
    return 0;
}

// === Ring 3 Shell Syscalls ===

static int syscall_putchar(uint32_t ch, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    vga_putc((char)ch);
    return 0;
}

static int syscall_getchar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    // Blocking keyboard read — busy-poll.
    // We do NOT enable interrupts here because the timer's scheduler_tick()
    // can call schedule() + switch_context() which would corrupt our return
    // path (we're inside the INT 0x80 handler). Polling with cli is safe
    // since the keyboard controller buffers scancodes for us.
    while (1) {
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
        __asm__ volatile ("pause");
    }
}

static int syscall_kcmd(uint32_t cmd_ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    const char* cmd = (const char*)cmd_ptr;
    if (!cmd || !*cmd) return -1;
    return execute_command(cmd);
}

static int syscall_getcwd(uint32_t buf_ptr, uint32_t len, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    if (!buf || len == 0) return -1;
    const char* cwd = vfs_getcwd();
    if (!cwd) { buf[0] = '/'; buf[1] = '\0'; return 1; }
    uint32_t slen = strlen(cwd);
    if (slen >= len) slen = len - 1;
    memcpy(buf, cwd, slen);
    buf[slen] = '\0';
    return (int)slen;
}

static int syscall_setcolor(uint32_t color, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    vga_set_color((uint8_t)color);
    return 0;
}

static int syscall_clear(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_clear_all();
    return 0;
}

static int syscall_getuser(uint32_t buf_ptr, uint32_t len, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    if (!buf || len == 0) return -1;
    session_t* session = user_get_session();
    if (!session || !session->user) {
        // No user logged in - return "?" as placeholder
        buf[0] = '?'; buf[1] = '\0';
        return 1;
    }
    uint32_t slen = strlen(session->user->username);
    if (slen >= len) slen = len - 1;
    memcpy(buf, session->user->username, slen);
    buf[slen] = '\0';
    return (int)slen;
}

static int syscall_isroot(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return user_is_root();
}

static int syscall_login(uint32_t user_ptr, uint32_t pass_ptr, uint32_t c, uint32_t d, uint32_t e) {
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

static int syscall_logout(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    user_logout();
    return 0;
}

static int syscall_getversion(uint32_t buf_ptr, uint32_t len, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    if (!buf || len == 0) return -1;
    
    const char* version = AOS_VERSION_SHORT;
    size_t vlen = strlen(version);
    if (vlen >= len) vlen = len - 1;
    memcpy(buf, version, vlen);
    buf[vlen] = '\0';
    return (int)vlen;
}

static int syscall_isfirsttime(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
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

static int syscall_getuserflags(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    session_t* session = user_get_session();
    if (!session || !session->user) return 0;
    return (int)session->user->flags;
}

static int syscall_setpassword(uint32_t user_ptr, uint32_t pass_ptr, uint32_t c, uint32_t d, uint32_t e) {
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

static int syscall_getunformatted(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    extern int unformatted_disk_detected;
    int val = unformatted_disk_detected;
    unformatted_disk_detected = 0;  // Clear after reading
    return val;
}

static int syscall_gethomedir(uint32_t buf_ptr, uint32_t len, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    char* buf = (char*)buf_ptr;
    if (!buf || len == 0) return -1;
    
    session_t* session = user_get_session();
    if (!session || !session->user) return -1;
    
    size_t hlen = strlen(session->user->home_dir);
    if (hlen >= len) hlen = len - 1;
    memcpy(buf, session->user->home_dir, hlen);
    buf[hlen] = '\0';
    return (int)hlen;
}

static int syscall_vga_enable_cursor(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_enable_cursor();
    return 0;
}

static int syscall_vga_disable_cursor(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_disable_cursor();
    return 0;
}

static int syscall_vga_set_cursor_style(uint32_t style, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)b; (void)c; (void)d; (void)e;
    vga_set_cursor_style((vga_cursor_style_t)style);
    return 0;
}

static int syscall_vga_get_pos(uint32_t row_ptr, uint32_t col_ptr, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    uint8_t* row = (uint8_t*)row_ptr;
    uint8_t* col = (uint8_t*)col_ptr;
    if (row) *row = vga_get_row();
    if (col) *col = vga_get_col();
    return 0;
}

static int syscall_vga_set_pos(uint32_t row, uint32_t col, uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;
    vga_set_position((uint8_t)row, (uint8_t)col);
    return 0;
}

static int syscall_vga_backspace(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_backspace();
    return 0;
}

static int syscall_vga_scroll_up_view(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_scroll_up_view();
    return 0;
}

static int syscall_vga_scroll_down(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_scroll_down();
    return 0;
}

static int syscall_vga_scroll_to_bottom(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    vga_scroll_to_bottom();
    return 0;
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
};

// System call interrupt handler (INT 0x80)
void syscall_handler(void* regs_ptr) {
    arch_registers_t* regs = (arch_registers_t*)regs_ptr;
    
    // System call number in EAX, arguments in EBX, ECX, EDX, ESI, EDI
    uint32_t syscall_num = regs->eax;
    
    if (syscall_num >= SYSCALL_COUNT) {
        serial_puts("Invalid syscall number: ");
        regs->eax = (uint32_t)-1;  // Return error
        return;
    }
    
    // Get current process and check sandbox permissions (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        // Check if syscall is allowed by sandbox filter
        if (!syscall_check_allowed(syscall_num, proc->sandbox.syscall_filter)) {
            serial_puts("Syscall blocked by sandbox: ");
            regs->eax = (uint32_t)-1;  // Return error
            return;
        }
        
        // Check CPU time limit
        if (!resource_check_time(proc->pid)) {
            serial_puts("Process exceeded CPU time limit\n");
            process_exit(-1);
            regs->eax = (uint32_t)-1;
            return;
        }
    }
    
    // Call the appropriate handler
    syscall_handler_t handler = syscall_table[syscall_num];
    int result = handler(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);
    
    // Return value in EAX
    regs->eax = (uint32_t)result;
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
