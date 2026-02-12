/*
 * === AOS HEADER BEGIN ===
 * include/fd.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef FD_H
#define FD_H

#include <process.h>

// File operations flags (compatible with VFS)
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0100
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

/**
 * Initialize standard file descriptors for a process
 */
void fd_init_stdio(process_t* proc);

/**
 * Duplicate file descriptor
 */
int fd_dup(int oldfd);

/**
 * Duplicate file descriptor to specific FD
 */
int fd_dup2(int oldfd, int newfd);

/**
 * Close file descriptor
 */
int fd_close(int fd);

/**
 * Convert process FD to VFS FD
 */
int fd_to_vfs(int fd);

#endif // FD_H
