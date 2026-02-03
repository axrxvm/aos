/*
 * === AOS HEADER BEGIN ===
 * ./include/sandbox.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef SANDBOX_H
#define SANDBOX_H

#include <stdint.h>
#include <stddef.h>

// aOS "Cage" for sandboxed environments

// Cage types (isolation levels)
typedef enum {
    CAGE_NONE = 0,          // No isolation (kernel/system processes)
    CAGE_LIGHT = 1,         // Light restrictions (syscall filtering only)
    CAGE_STANDARD = 2,      // Standard isolation (syscall + resource limits)
    CAGE_STRICT = 3,        // Strict isolation (syscall + resource + root cage)
    CAGE_LOCKED = 4         // Locked cage (minimal syscalls, immutable root)
} cage_level_t;

// Syscall filter flags (bitfield)
#define ALLOW_IO_READ       (1 << 0)   // Allow read operations
#define ALLOW_IO_WRITE      (1 << 1)   // Allow write operations
#define ALLOW_IO_EXEC       (1 << 2)   // Allow execute operations
#define ALLOW_PROCESS       (1 << 3)   // Allow process creation/control
#define ALLOW_MEMORY        (1 << 4)   // Allow memory operations
#define ALLOW_NETWORK       (1 << 5)   // Allow network access (future)
#define ALLOW_DEVICE        (1 << 6)   // Allow device access
#define ALLOW_TIME          (1 << 7)   // Allow time operations
#define ALLOW_IPC           (1 << 8)   // Allow inter-process communication

// Default permission sets
#define ALLOW_MINIMAL       (ALLOW_IO_READ | ALLOW_TIME)
#define ALLOW_NORMAL        (ALLOW_IO_READ | ALLOW_IO_WRITE | ALLOW_PROCESS | ALLOW_MEMORY | ALLOW_TIME)
#define ALLOW_SYSTEM        (0xFFFFFFFF)  // All permissions

// Resource limits for sandboxed processes
typedef struct {
    uint32_t max_memory;        // Max memory in bytes (0 = unlimited)
    uint32_t max_files;         // Max open files (0 = unlimited)
    uint32_t max_processes;     // Max child processes (0 = unlimited)
    uint32_t max_cpu_time;      // Max CPU time in milliseconds (0 = unlimited)
} resource_limits_t;

// Sandbox configuration (per process)
typedef struct {
    cage_level_t cage_level;        // Isolation level
    uint32_t syscall_filter;        // Allowed syscall categories (bitfield)
    char cageroot[256];             // Root directory for cage (empty = no cage)
    resource_limits_t limits;       // Resource limits
    uint32_t flags;                 // Additional flags
} sandbox_t;

// Sandbox flags
#define SANDBOX_READONLY    (1 << 0)    // Cage is read-only
#define SANDBOX_NOEXEC      (1 << 1)    // No execution allowed
#define SANDBOX_NONET       (1 << 2)    // No network access
#define SANDBOX_IMMUTABLE   (1 << 3)    // Cannot change sandbox after creation

// Sandbox initialization
void sandbox_init(void);

// Sandbox operations
int sandbox_create(sandbox_t* sandbox, cage_level_t level);
int sandbox_apply(int pid, const sandbox_t* sandbox);
int sandbox_get(int pid, sandbox_t* sandbox);
int sandbox_modify(int pid, const sandbox_t* sandbox);

// Cage operations (aOS's chroot alternative)
int cage_set_root(int pid, const char* path);
int cage_get_root(int pid, char* buffer, size_t size);
int cage_enter(int pid);  // Enter cage (cannot exit)

// Syscall filtering
int syscall_check_allowed(int syscall_num, uint32_t filter);
int syscall_filter_add(int pid, uint32_t permissions);
int syscall_filter_remove(int pid, uint32_t permissions);

// Resource limit checks
int resource_check_memory(int pid, uint32_t requested);
int resource_check_files(int pid);
int resource_check_processes(int pid);
int resource_check_time(int pid);

// Predefined sandbox profiles
extern const sandbox_t SANDBOX_PROFILE_MINIMAL;
extern const sandbox_t SANDBOX_PROFILE_STANDARD;
extern const sandbox_t SANDBOX_PROFILE_TRUSTED;
extern const sandbox_t SANDBOX_PROFILE_SYSTEM;

#endif // SANDBOX_H
