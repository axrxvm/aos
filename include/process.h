/*
 * === AOS HEADER BEGIN ===
 * include/process.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <arch/i386/paging.h>
#include <vmm.h>
#include <sandbox.h>
#include <fileperm.h>

// Process states
typedef enum {
    PROCESS_READY,      // Ready to run
    PROCESS_RUNNING,    // Currently executing
    PROCESS_BLOCKED,    // Waiting for I/O or event
    PROCESS_SLEEPING,   // Sleeping for a timeout
    PROCESS_ZOMBIE,     // Terminated, waiting for parent
    PROCESS_DEAD        // Can be reclaimed
} process_state_t;

// Task categories tracked by the process system
typedef enum {
    TASK_TYPE_PROCESS = 0,   // Regular schedulable userspace process
    TASK_TYPE_KERNEL,        // Kernel control task
    TASK_TYPE_SHELL,         // Interactive shell task
    TASK_TYPE_COMMAND,       // Command execution task
    TASK_TYPE_SERVICE,       // Init/service-managed task
    TASK_TYPE_DRIVER,        // Device driver task
    TASK_TYPE_MODULE,        // Kernel module task
    TASK_TYPE_SUBSYSTEM      // Core subsystem task
} task_type_t;

// Process priority levels
#define PRIORITY_IDLE       0
#define PRIORITY_LOW        1
#define PRIORITY_NORMAL     2
#define PRIORITY_HIGH       3
#define PRIORITY_REALTIME   4

// Maximum number of processes
#define MAX_PROCESSES 256

// Maximum number of open files per process
#define MAX_OPEN_FILES 16

// Standard file descriptors
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// Process ID type
typedef int pid_t;

// CPU context saved during context switch
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cr3;           // Page directory physical address
    uint16_t cs, ds, es, fs, gs, ss;
} cpu_context_t;

// Process Control Block (PCB)
typedef struct process {
    pid_t pid;                      // Process ID
    pid_t parent_pid;               // Parent process ID
    char name[64];                  // Process name
    
    process_state_t state;          // Current state
    task_type_t task_type;          // Task category
    uint8_t schedulable;            // 1=scheduler-managed execution context
    int priority;                   // Scheduling priority
    uint32_t time_slice;            // Remaining time slice
    uint32_t total_time;            // Total CPU time used
    
    cpu_context_t context;          // Saved CPU state
    address_space_t* address_space; // Virtual memory space
    
    uint32_t kernel_stack;          // Kernel stack pointer
    uint32_t user_stack;            // User stack pointer
    
    int file_descriptors[MAX_OPEN_FILES];  // Open file descriptors
    uint32_t privilege_level;       // 0=kernel, 3=user
    
    int exit_status;                // Exit status code
    uint32_t wake_time;             // Wake up time (for sleeping)
    
    // Security and isolation (v0.7.3)
    sandbox_t sandbox;              // Sandbox/cage configuration
    uint32_t owner_id;              // Process owner ID
    owner_type_t owner_type;        // Process owner type
    uint32_t memory_used;           // Current memory usage
    uint32_t files_open;            // Number of open files
    uint32_t children_count;        // Number of child processes
    
    struct process* next;           // Next in queue
    struct process* parent;         // Parent process
    struct process* children;       // First child
    struct process* sibling;        // Next sibling
} process_t;

// Initialize process manager
void init_process_manager(void);

// Process lifecycle
pid_t process_create(const char* name, void (*entry_point)(void), int priority);
pid_t process_register_kernel_task(const char* name, task_type_t type, int priority);
int process_finish_kernel_task(pid_t pid, int status);
int process_mark_task_state(pid_t pid, process_state_t state);
int process_set_current_identity(const char* name, task_type_t type, int priority, uint32_t privilege_level);
void process_exit(int status);
int process_fork(void);
int process_execve(const char* path, char* const argv[], char* const envp[]);

// Process control
int process_kill(int pid, int signal);
int process_waitpid(int pid, int* status, int options);
void process_yield(void);
void process_sleep(uint32_t milliseconds);

// Process queries
int process_getpid(void);
process_t* process_get_current(void);
process_t* process_get_by_pid(pid_t pid);
int process_for_each(int (*callback)(process_t* proc, void* ctx), void* ctx);
const char* process_task_type_name(task_type_t type);

// Scheduler
void schedule(void);
void scheduler_tick(void);
void process_set_preempt_disabled(int disabled);
int process_is_preempt_disabled(void);

// Memory management
void* process_sbrk(int increment);

// Context switching (defined in assembly)
extern void switch_context(cpu_context_t* old_context, cpu_context_t* new_context);

#endif // PROCESS_H
