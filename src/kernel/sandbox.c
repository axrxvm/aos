/*
 * === AOS HEADER BEGIN ===
 * src/kernel/sandbox.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <sandbox.h>
#include <string.h>
#include <serial.h>
#include <syscall.h>
#include <process.h>

// Forward declarations for process-level functions
extern int sandbox_apply_to_process(int pid, const sandbox_t* sandbox);
extern int sandbox_get_from_process(int pid, sandbox_t* sandbox);
extern int cage_set_root_for_process(int pid, const char* path);
extern int cage_get_root_for_process(int pid, char* buffer, size_t size);
extern int resource_check_memory_for_process(int pid, uint32_t requested);
extern int resource_check_files_for_process(int pid);
extern int resource_check_processes_for_process(int pid);
extern int resource_check_time_for_process(int pid);

// Predefined sandbox profiles
const sandbox_t SANDBOX_PROFILE_MINIMAL = {
    .cage_level = CAGE_LOCKED,
    .syscall_filter = ALLOW_MINIMAL,
    .cageroot = "",
    .limits = {
        .max_memory = 1024 * 1024,      // 1MB
        .max_files = 4,
        .max_processes = 0,
        .max_cpu_time = 5000            // 5 seconds
    },
    .flags = SANDBOX_READONLY | SANDBOX_NOEXEC
};

const sandbox_t SANDBOX_PROFILE_STANDARD = {
    .cage_level = CAGE_STANDARD,
    .syscall_filter = ALLOW_NORMAL,
    .cageroot = "",
    .limits = {
        .max_memory = 16 * 1024 * 1024, // 16MB
        .max_files = 32,
        .max_processes = 8,
        .max_cpu_time = 60000           // 1 minute
    },
    .flags = 0
};

const sandbox_t SANDBOX_PROFILE_TRUSTED = {
    .cage_level = CAGE_LIGHT,
    .syscall_filter = ALLOW_NORMAL | ALLOW_DEVICE,
    .cageroot = "",
    .limits = {
        .max_memory = 64 * 1024 * 1024, // 64MB
        .max_files = 128,
        .max_processes = 32,
        .max_cpu_time = 0               // Unlimited
    },
    .flags = 0
};

const sandbox_t SANDBOX_PROFILE_SYSTEM = {
    .cage_level = CAGE_NONE,
    .syscall_filter = ALLOW_SYSTEM,
    .cageroot = "",
    .limits = {
        .max_memory = 0,                // Unlimited
        .max_files = 0,
        .max_processes = 0,
        .max_cpu_time = 0
    },
    .flags = 0
};

// Syscall category mapping
static const uint32_t syscall_categories[SYSCALL_COUNT] = {
    [SYS_EXIT]      = ALLOW_PROCESS,
    [SYS_FORK]      = ALLOW_PROCESS,
    [SYS_READ]      = ALLOW_IO_READ,
    [SYS_WRITE]     = ALLOW_IO_WRITE,
    [SYS_OPEN]      = ALLOW_IO_READ,
    [SYS_CLOSE]     = ALLOW_IO_READ | ALLOW_IO_WRITE,
    [SYS_WAITPID]   = ALLOW_PROCESS,
    [SYS_EXECVE]    = ALLOW_IO_EXEC | ALLOW_PROCESS,
    [SYS_GETPID]    = ALLOW_PROCESS,
    [SYS_KILL]      = ALLOW_PROCESS,
    [SYS_LSEEK]     = ALLOW_IO_READ | ALLOW_IO_WRITE,
    [SYS_READDIR]   = ALLOW_IO_READ,
    [SYS_MKDIR]     = ALLOW_IO_WRITE,
    [SYS_RMDIR]     = ALLOW_IO_WRITE,
    [SYS_UNLINK]    = ALLOW_IO_WRITE,
    [SYS_STAT]      = ALLOW_IO_READ,
    [SYS_SBRK]      = ALLOW_MEMORY,
    [SYS_SLEEP]     = ALLOW_TIME,
    [SYS_YIELD]     = ALLOW_TIME,
};

// Initialize sandbox system
void sandbox_init(void) {
    serial_puts("Initializing sandbox system (Cage model)...\n");
    serial_puts("Sandbox system initialized.\n");
}

// Create a new sandbox configuration
int sandbox_create(sandbox_t* sandbox, cage_level_t level) {
    if (!sandbox) {
        return -1;
    }

    memset(sandbox, 0, sizeof(sandbox_t));
    sandbox->cage_level = level;

    // Set defaults based on level
    switch (level) {
        case CAGE_NONE:
            sandbox->syscall_filter = ALLOW_SYSTEM;
            break;
        case CAGE_LIGHT:
            sandbox->syscall_filter = ALLOW_NORMAL | ALLOW_IPC;
            break;
        case CAGE_STANDARD:
            sandbox->syscall_filter = ALLOW_NORMAL;
            sandbox->limits.max_memory = 16 * 1024 * 1024;
            sandbox->limits.max_files = 32;
            sandbox->limits.max_processes = 8;
            break;
        case CAGE_STRICT:
            sandbox->syscall_filter = ALLOW_IO_READ | ALLOW_IO_WRITE | ALLOW_TIME;
            sandbox->limits.max_memory = 8 * 1024 * 1024;
            sandbox->limits.max_files = 16;
            sandbox->limits.max_processes = 4;
            break;
        case CAGE_LOCKED:
            sandbox->syscall_filter = ALLOW_MINIMAL;
            sandbox->limits.max_memory = 1 * 1024 * 1024;
            sandbox->limits.max_files = 4;
            sandbox->limits.max_processes = 0;
            sandbox->flags = SANDBOX_READONLY | SANDBOX_NOEXEC;
            break;
        default:
            return -1;
    }

    return 0;
}

// Apply sandbox to process (implemented in process.c)
int sandbox_apply(int pid, const sandbox_t* sandbox) {
    if (!sandbox) {
        return -1;
    }
    return sandbox_apply_to_process(pid, sandbox);
}

// Get sandbox configuration for process
int sandbox_get(int pid, sandbox_t* sandbox) {
    if (!sandbox) {
        return -1;
    }
    return sandbox_get_from_process(pid, sandbox);
}

// Modify sandbox configuration
int sandbox_modify(int pid, const sandbox_t* sandbox) {
    if (!sandbox) {
        return -1;
    }
    // Check if sandbox is immutable
    sandbox_t current;
    if (sandbox_get(pid, &current) == 0) {
        if (current.flags & SANDBOX_IMMUTABLE) {
            return -1; // Cannot modify immutable sandbox
        }
    }
    return sandbox_apply(pid, sandbox);
}

// Set cage root (aOS's chroot alternative)
int cage_set_root(int pid, const char* path) {
    if (!path) {
        return -1;
    }
    return cage_set_root_for_process(pid, path);
}

// Get cage root
int cage_get_root(int pid, char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return -1;
    }
    return cage_get_root_for_process(pid, buffer, size);
}

// Enter cage (irreversible)
int cage_enter(int pid) {
    // Mark the cage as entered by setting immutable flag
    sandbox_t sb;
    if (sandbox_get(pid, &sb) == 0) {
        sb.flags |= SANDBOX_IMMUTABLE;
        return sandbox_apply(pid, &sb);
    }
    return -1;
}

// Check if syscall is allowed by filter
int syscall_check_allowed(int syscall_num, uint32_t filter) {
    if (syscall_num < 0 || syscall_num >= SYSCALL_COUNT) {
        return 0; // Invalid syscall
    }

    // Get required category for this syscall
    uint32_t required = syscall_categories[syscall_num];
    
    // Check if all required categories are present in filter
    return (filter & required) == required;
}

// Add permissions to syscall filter
int syscall_filter_add(int pid, uint32_t permissions) {
    (void)pid;
    (void)permissions;
    // This will be implemented in process.c
    return 0;
}

// Remove permissions from syscall filter
int syscall_filter_remove(int pid, uint32_t permissions) {
    (void)pid;
    (void)permissions;
    // This will be implemented in process.c
    return 0;
}

// Check resource limits
int resource_check_memory(int pid, uint32_t requested) {
    return resource_check_memory_for_process(pid, requested);
}

int resource_check_files(int pid) {
    return resource_check_files_for_process(pid);
}

int resource_check_processes(int pid) {
    return resource_check_processes_for_process(pid);
}

int resource_check_time(int pid) {
    return resource_check_time_for_process(pid);
}
