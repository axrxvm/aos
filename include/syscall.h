/*
 * === AOS HEADER BEGIN ===
 * include/syscall.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <fs/vfs.h>

// System call numbers
#define SYS_EXIT        0
#define SYS_FORK        1
#define SYS_READ        2
#define SYS_WRITE       3
#define SYS_OPEN        4
#define SYS_CLOSE       5
#define SYS_WAITPID     6
#define SYS_EXECVE      7
#define SYS_GETPID      8
#define SYS_KILL        9
#define SYS_LSEEK       10
#define SYS_READDIR     11
#define SYS_MKDIR       12
#define SYS_RMDIR       13
#define SYS_UNLINK      14
#define SYS_STAT        15
#define SYS_SBRK        16
#define SYS_SLEEP       17
#define SYS_YIELD       18
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

#define SYSCALL_COUNT   44

// Initialize syscall handler
void init_syscalls(void);

// System call implementations (kernel-side)
void sys_exit(int status);
int sys_fork(void);
int sys_read(int fd, void* buffer, uint32_t size);
int sys_write(int fd, const void* buffer, uint32_t size);
int sys_open(const char* path, uint32_t flags);
int sys_close(int fd);
int sys_waitpid(int pid, int* status, int options);
int sys_execve(const char* path, char* const argv[], char* const envp[]);
int sys_getpid(void);
int sys_kill(int pid, int signal);
int sys_lseek(int fd, int offset, int whence);
int sys_readdir(int fd, dirent_t* dirent);
int sys_mkdir(const char* path);
int sys_rmdir(const char* path);
int sys_unlink(const char* path);
int sys_stat(const char* path, stat_t* stat);
void* sys_sbrk(int increment);
void sys_sleep(uint32_t milliseconds);
void sys_yield(void);

#endif // SYSCALL_H
