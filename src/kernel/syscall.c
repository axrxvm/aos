/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/syscall.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <syscall.h>
#include <fs/vfs.h>
#include <arch.h>
#include <arch_types.h>
#include <serial.h>
#include <sandbox.h>
#include <process.h>

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

// System call table
static syscall_handler_t syscall_table[SYSCALL_COUNT] = {
    syscall_exit,      // 0
    syscall_fork,      // 1
    syscall_read,      // 2
    syscall_write,     // 3
    syscall_open,      // 4
    syscall_close,     // 5
    syscall_waitpid,   // 6
    syscall_execve,    // 7
    syscall_getpid,    // 8
    syscall_kill,      // 9
    syscall_lseek,     // 10
    syscall_readdir,   // 11
    syscall_mkdir,     // 12
    syscall_rmdir,     // 13
    syscall_unlink,    // 14
    syscall_stat,      // 15
    syscall_sbrk,      // 16
    syscall_sleep,     // 17
    syscall_yield,     // 18
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
