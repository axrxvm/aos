/*
 * === AOS HEADER BEGIN ===
 * src/kernel/fd.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <process.h>
#include <fs/vfs.h>
#include <serial.h>

// Get current process
extern process_t* process_get_current(void);

/**
 * Initialize standard file descriptors for a process
 * stdin (0), stdout (1), stderr (2)
 */
void fd_init_stdio(process_t* proc) {
    if (!proc) return;
    
    // Open /dev/tty for stdin (read-only)
    proc->file_descriptors[STDIN_FILENO] = vfs_open("/dev/tty", O_RDONLY);
    if (proc->file_descriptors[STDIN_FILENO] < 0) {
        serial_puts("Warning: Failed to open stdin\n");
    }
    
    // Open /dev/tty for stdout (write-only)
    proc->file_descriptors[STDOUT_FILENO] = vfs_open("/dev/tty", O_WRONLY);
    if (proc->file_descriptors[STDOUT_FILENO] < 0) {
        serial_puts("Warning: Failed to open stdout\n");
    }
    
    // Open /dev/tty for stderr (write-only)
    proc->file_descriptors[STDERR_FILENO] = vfs_open("/dev/tty", O_WRONLY);
    if (proc->file_descriptors[STDERR_FILENO] < 0) {
        serial_puts("Warning: Failed to open stderr\n");
    }
}

/**
 * Duplicate a file descriptor
 */
int fd_dup(int oldfd) {
    process_t* proc = process_get_current();
    if (!proc || oldfd < 0 || oldfd >= MAX_OPEN_FILES) {
        return -1;
    }
    
    if (proc->file_descriptors[oldfd] == -1) {
        return -1;  // Old FD not open
    }
    
    // Find first available FD
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (proc->file_descriptors[i] == -1) {
            proc->file_descriptors[i] = proc->file_descriptors[oldfd];
            // TODO: Increment reference count in VFS
            return i;
        }
    }
    
    return -1;  // No free FDs
}

/**
 * Duplicate file descriptor to specific FD
 */
int fd_dup2(int oldfd, int newfd) {
    process_t* proc = process_get_current();
    if (!proc || oldfd < 0 || oldfd >= MAX_OPEN_FILES ||
        newfd < 0 || newfd >= MAX_OPEN_FILES) {
        return -1;
    }
    
    if (proc->file_descriptors[oldfd] == -1) {
        return -1;  // Old FD not open
    }
    
    if (oldfd == newfd) {
        return newfd;  // Already the same
    }
    
    // Close newfd if it's open
    if (proc->file_descriptors[newfd] != -1) {
        vfs_close(proc->file_descriptors[newfd]);
    }
    
    proc->file_descriptors[newfd] = proc->file_descriptors[oldfd];
    // TODO: Increment reference count in VFS
    
    return newfd;
}

/**
 * Close a file descriptor
 */
int fd_close(int fd) {
    process_t* proc = process_get_current();
    if (!proc || fd < 0 || fd >= MAX_OPEN_FILES) {
        return -1;
    }
    
    if (proc->file_descriptors[fd] == -1) {
        return -1;  // Already closed
    }
    
    int result = vfs_close(proc->file_descriptors[fd]);
    proc->file_descriptors[fd] = -1;
    
    return result;
}

/**
 * Get VFS file descriptor from process FD
 */
int fd_to_vfs(int fd) {
    process_t* proc = process_get_current();
    if (!proc || fd < 0 || fd >= MAX_OPEN_FILES) {
        return -1;
    }
    
    return proc->file_descriptors[fd];
}
