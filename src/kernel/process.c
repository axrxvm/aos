/*
 * === AOS HEADER BEGIN ===
 * src/kernel/process.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <process.h>
#include <string.h>
#include <serial.h>
#include <arch.h>
#include <arch_paging.h>
#include <pmm.h>
#include <vmm.h>
#include <panic.h>
#include <elf.h>
#include <sandbox.h>
#include <fileperm.h>
#include <init.h>
#include <kmodule.h>

// Process table
static process_t process_table[MAX_PROCESSES];
static process_t* current_process = NULL;
static process_t* idle_process = NULL;
static pid_t next_pid = 1;

// Ready queues (one per priority level)
static process_t* ready_queue[5] = {NULL, NULL, NULL, NULL, NULL};

// Ticks counter for scheduler
static uint32_t scheduler_ticks = 0;
static volatile uint32_t preempt_disable_depth = 0;

// Time slice per priority (in ticks)
static const uint32_t time_slices[5] = {
    1,   // IDLE
    5,   // LOW
    10,  // NORMAL
    15,  // HIGH
    20   // REALTIME
};

// Helper functions
static int clamp_priority(int priority) {
    if (priority < PRIORITY_IDLE) return PRIORITY_IDLE;
    if (priority > PRIORITY_REALTIME) return PRIORITY_REALTIME;
    return priority;
}

const char* process_task_type_name(task_type_t type) {
    switch (type) {
        case TASK_TYPE_PROCESS: return "process";
        case TASK_TYPE_KERNEL: return "kernel";
        case TASK_TYPE_SHELL: return "shell";
        case TASK_TYPE_COMMAND: return "command";
        case TASK_TYPE_SERVICE: return "service";
        case TASK_TYPE_DRIVER: return "driver";
        case TASK_TYPE_MODULE: return "module";
        case TASK_TYPE_SUBSYSTEM: return "subsystem";
        default: return "unknown";
    }
}

static void enqueue_process(process_t* proc) {
    if (!proc || !proc->schedulable) return;
    
    int priority = clamp_priority(proc->priority);
    
    proc->next = NULL;
    proc->state = PROCESS_READY;
    
    if (!ready_queue[priority]) {
        ready_queue[priority] = proc;
    } else {
        process_t* current = ready_queue[priority];
        while (current->next) {
            current = current->next;
        }
        current->next = proc;
    }
}

static process_t* dequeue_process(int priority) {
    if (priority < PRIORITY_IDLE || priority > PRIORITY_REALTIME) return NULL;
    if (!ready_queue[priority]) return NULL;
    
    process_t* proc = ready_queue[priority];
    ready_queue[priority] = proc->next;
    proc->next = NULL;
    
    return proc;
}

static process_t* allocate_process(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_DEAD) {
            memset(&process_table[i], 0, sizeof(process_t));
            process_table[i].pid = next_pid++;
            return &process_table[i];
        }
    }
    return NULL;
}

static void idle_task(void) {
    while (1) {
        asm volatile("hlt");  // Halt until next interrupt
    }
}

// Initialize process manager
void init_process_manager(void) {
    serial_puts("Initializing process manager...\n");
    
    // Initialize process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].state = PROCESS_DEAD;
        process_table[i].pid = 0;
    }
    
    // Create idle process
    idle_process = allocate_process();
    if (!idle_process) {
        panic("Failed to create idle process");
    }
    
    strcpy(idle_process->name, "idle");
    idle_process->task_type = TASK_TYPE_KERNEL;
    idle_process->schedulable = 1;
    idle_process->priority = PRIORITY_IDLE;
    idle_process->state = PROCESS_READY;
    idle_process->parent_pid = 0;
    idle_process->address_space = kernel_address_space;
    void* idle_stack = kmalloc(4096);
    if (!idle_stack) {
        panic("Failed to allocate idle stack");
    }
    idle_process->context.eip = (uintptr_t)idle_task;
    idle_process->context.esp = (uintptr_t)idle_stack + 4096;  // 4KB kernel stack
    idle_process->context.ebp = idle_process->context.esp;
    idle_process->context.eflags = 0x202;  // IF flag set
#ifdef ARCH_HAS_SEGMENTATION
    idle_process->context.cs = arch_get_kernel_code_segment();
    idle_process->context.ds = arch_get_kernel_data_segment();
    idle_process->context.es = arch_get_kernel_data_segment();
    idle_process->context.fs = arch_get_kernel_data_segment();
    idle_process->context.gs = arch_get_kernel_data_segment();
    idle_process->context.ss = arch_get_kernel_data_segment();
#endif
    idle_process->kernel_stack = idle_process->context.esp;
    
    // Initialize sandbox for idle process (system level)
    sandbox_create(&idle_process->sandbox, CAGE_NONE);
    idle_process->owner_type = OWNER_SYSTEM;
    idle_process->owner_id = 0;
    idle_process->memory_used = 0;
    idle_process->files_open = 0;
    idle_process->children_count = 0;
    idle_process->privilege_level = 0;  // Kernel mode (ring 0)
    
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        idle_process->file_descriptors[i] = -1;
    }
    
    enqueue_process(idle_process);
    
    // Create initial kernel process (current context)
    current_process = allocate_process();
    if (!current_process) {
        panic("Failed to create initial process");
    }
    
    strcpy(current_process->name, "kernel");
    current_process->task_type = TASK_TYPE_KERNEL;
    current_process->schedulable = 1;
    current_process->priority = PRIORITY_NORMAL;
    current_process->state = PROCESS_RUNNING;
    current_process->parent_pid = 0;
    current_process->address_space = kernel_address_space;
    current_process->time_slice = time_slices[PRIORITY_NORMAL];
    
    // Initialize sandbox for kernel process (system level)
    sandbox_create(&current_process->sandbox, CAGE_NONE);
    current_process->owner_type = OWNER_SYSTEM;
    current_process->owner_id = 0;
    current_process->memory_used = 0;
    current_process->files_open = 0;
    current_process->children_count = 0;
    current_process->privilege_level = 0;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        current_process->file_descriptors[i] = -1;
    }
    
    serial_puts("Process manager initialized.\n");
}

// Get current process
process_t* process_get_current(void) {
    return current_process;
}

// Get process by PID
process_t* process_get_by_pid(pid_t pid) {
    if (pid <= 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROCESS_DEAD) {
            return &process_table[i];
        }
    }
    return NULL;
}

int process_for_each(int (*callback)(process_t* proc, void* ctx), void* ctx) {
    if (!callback) {
        return -1;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = &process_table[i];
        if (proc->pid != 0 && proc->state != PROCESS_DEAD) {
            int ret = callback(proc, ctx);
            if (ret != 0) {
                return ret;
            }
        }
    }

    return 0;
}

// Get current PID
int process_getpid(void) {
    if (current_process) {
        return current_process->pid;
    }
    return -1;
}

pid_t process_register_kernel_task(const char* name, task_type_t type, int priority) {
    process_t* proc = allocate_process();
    if (!proc) {
        return -1;
    }

    if (!name || !*name) {
        name = "kernel-task";
    }

    strncpy(proc->name, name, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    proc->task_type = type;
    proc->schedulable = 0;
    proc->priority = clamp_priority(priority);
    proc->state = PROCESS_RUNNING;
    proc->parent_pid = current_process ? current_process->pid : 0;
    proc->address_space = kernel_address_space;
    proc->privilege_level = 0;
    proc->time_slice = 0;

    sandbox_create(&proc->sandbox, CAGE_NONE);
    proc->owner_type = OWNER_SYSTEM;
    proc->owner_id = 0;
    proc->memory_used = 0;
    proc->files_open = 0;
    proc->children_count = 0;

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        proc->file_descriptors[i] = -1;
    }

    if (current_process) {
        current_process->children_count++;
        proc->parent = current_process;
    }

    return proc->pid;
}

int process_finish_kernel_task(pid_t pid, int status) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc || proc->schedulable || proc == current_process) {
        return -1;
    }

    if (proc->parent && proc->parent->children_count > 0) {
        proc->parent->children_count--;
    }
    proc->exit_status = status;
    proc->state = PROCESS_DEAD;
    return 0;
}

int process_mark_task_state(pid_t pid, process_state_t state) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return -1;
    }

    if (!proc->schedulable && state == PROCESS_READY) {
        return -1;
    }

    proc->state = state;

    if (proc->schedulable && state == PROCESS_READY) {
        enqueue_process(proc);
    }

    return 0;
}

int process_set_current_identity(const char* name, task_type_t type, int priority, uint32_t privilege_level) {
    if (!current_process) {
        return -1;
    }

    if (name && *name) {
        strncpy(current_process->name, name, sizeof(current_process->name) - 1);
        current_process->name[sizeof(current_process->name) - 1] = '\0';
    }

    current_process->task_type = type;
    current_process->priority = clamp_priority(priority);
    current_process->time_slice = time_slices[current_process->priority];
    current_process->privilege_level = privilege_level;
    current_process->schedulable = 1;

    // Ring 3 shell must be allowed to use terminal and input syscalls.
    if (privilege_level == 3 && type == TASK_TYPE_SHELL) {
        sandbox_create(&current_process->sandbox, CAGE_LIGHT);
        current_process->sandbox.syscall_filter |= (ALLOW_DEVICE | ALLOW_IPC);
    }
    return 0;
}

// Create a new process
pid_t process_create(const char* name, void (*entry_point)(void), int priority) {
    process_t* proc = allocate_process();
    if (!proc) {
        return -1;  // No free process slots
    }
    
    // Set basic info
    if (!name || !*name) {
        name = "task";
    }
    strncpy(proc->name, name, 63);
    proc->name[63] = '\0';
    proc->task_type = TASK_TYPE_PROCESS;
    proc->schedulable = 1;
    proc->priority = clamp_priority(priority);
    proc->state = PROCESS_READY;
    proc->parent_pid = current_process ? current_process->pid : 0;
    proc->time_slice = time_slices[proc->priority];
    
    // Create address space
    proc->address_space = create_address_space();
    if (!proc->address_space) {
        proc->state = PROCESS_DEAD;
        return -1;
    }
    
    // Allocate kernel stack
    void* kernel_stack_mem = kmalloc(8192);
    if (!kernel_stack_mem) {
        destroy_address_space(proc->address_space);
        proc->address_space = NULL;
        proc->state = PROCESS_DEAD;
        return -1;
    }
    proc->kernel_stack = (uintptr_t)kernel_stack_mem + 8192;  // 8KB kernel stack
    
    // Allocate user stack
    proc->user_stack = VMM_USER_STACK_TOP;
    vmm_alloc_at(proc->address_space, proc->user_stack - 8192, 8192, 
                 VMM_PRESENT | VMM_WRITE | VMM_USER);
    
    // Initialize context
    proc->context.eip = (uintptr_t)entry_point;
    proc->context.esp = proc->user_stack;
    proc->context.ebp = proc->user_stack;
    proc->context.eflags = 0x202;  // IF flag set
#ifdef ARCH_HAS_SEGMENTATION
    proc->context.cs = arch_get_user_code_segment() | 0x3;  // Ring 3
    proc->context.ds = arch_get_user_data_segment() | 0x3;
    proc->context.es = arch_get_user_data_segment() | 0x3;
    proc->context.fs = arch_get_user_data_segment() | 0x3;
    proc->context.gs = arch_get_user_data_segment() | 0x3;
    proc->context.ss = arch_get_user_data_segment() | 0x3;
#endif
    proc->context.cr3 = (uintptr_t)proc->address_space->page_dir->physical_addr;
    
    // Initialize sandbox (inherit from parent or use default)
    if (current_process && current_process->sandbox.cage_level != CAGE_NONE) {
        proc->sandbox = current_process->sandbox;  // Inherit parent's sandbox
    } else {
        sandbox_create(&proc->sandbox, CAGE_LIGHT);  // Default sandbox
    }
    
    // Initialize ownership (inherit from parent)
    if (current_process) {
        proc->owner_type = current_process->owner_type;
        proc->owner_id = current_process->owner_id;
        current_process->children_count++;
    } else {
        proc->owner_type = OWNER_USR;
        proc->owner_id = proc->pid;
    }
    
    proc->memory_used = 0;
    proc->files_open = 0;
    proc->children_count = 0;
    
    // Initialize file descriptors
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        proc->file_descriptors[i] = -1;  // -1 means closed
    }
    proc->privilege_level = 3;  // User mode by default
    
    // Add to ready queue
    enqueue_process(proc);
    
    return proc->pid;
}

// Exit current process
void process_exit(int status) {
    if (!current_process) return;
    
    current_process->exit_status = status;
    current_process->state = PROCESS_ZOMBIE;
    
    // Wake up parent if waiting
    if (current_process->parent) {
        if (current_process->parent->state == PROCESS_BLOCKED) {
            enqueue_process(current_process->parent);
        }
    }
    
    // Switch to another process
    schedule();
}

// Yield CPU to another process
void process_yield(void) {
    if (!current_process || !current_process->schedulable) return;
    
    // Put current process back in ready queue
    if (current_process->state == PROCESS_RUNNING) {
        enqueue_process(current_process);
    }
    
    // Force reschedule
    schedule();
}

// Sleep for milliseconds
void process_sleep(uint32_t milliseconds) {
    if (!current_process || !current_process->schedulable) return;
    
    current_process->state = PROCESS_SLEEPING;
    current_process->wake_time = scheduler_ticks + (milliseconds / 10);  // Assume 10ms tick
    
    schedule();
}

// Scheduler tick (called from timer interrupt)
void scheduler_tick(void) {
    scheduler_ticks++;

    // Service module timers on each scheduler tick.
    kmodule_v2_timer_tick();
    
    // Wake up sleeping processes
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].schedulable && process_table[i].state == PROCESS_SLEEPING) {
            if (scheduler_ticks >= process_table[i].wake_time) {
                enqueue_process(&process_table[i]);
            }
        }
    }
    
    // Decrement time slice of current process
    if (current_process && current_process->schedulable && current_process->state == PROCESS_RUNNING) {
        if (current_process->time_slice > 0) {
            current_process->time_slice--;
        }
        
        if (current_process->time_slice == 0) {
            // Time slice expired, reschedule unless kernel preemption is suppressed.
            if (preempt_disable_depth == 0) {
                schedule();
            }
        }
    }
}

void process_set_preempt_disabled(int disabled) {
    if (disabled) {
        preempt_disable_depth++;
    } else if (preempt_disable_depth > 0) {
        preempt_disable_depth--;
    }
}

int process_is_preempt_disabled(void) {
    return preempt_disable_depth != 0;
}

// Main scheduler
void schedule(void) {
    if (!current_process) {
        // First time scheduling
        for (int priority = 4; priority >= 0; priority--) {
            process_t* next = dequeue_process(priority);
            if (next) {
                current_process = next;
                current_process->state = PROCESS_RUNNING;
                current_process->time_slice = time_slices[current_process->priority];
                
                // Switch address space
                switch_address_space(current_process->address_space);
#ifdef ARCH_HAS_SEGMENTATION
                arch_set_kernel_stack(current_process->kernel_stack);
#endif
                
                return;
            }
        }
        panic("No processes to schedule!");
    }
    
    process_t* old_process = current_process;
    
    // Save old process state if still running
    if (old_process->schedulable && old_process->state == PROCESS_RUNNING) {
        enqueue_process(old_process);
    }
    
    // Find next process to run (highest priority first)
    process_t* next = NULL;
    for (int priority = 4; priority >= 0; priority--) {
        next = dequeue_process(priority);
        if (next) break;
    }
    
    if (!next) {
        // No process ready, continue with current or idle
        if (old_process->schedulable && old_process->state == PROCESS_RUNNING) {
            return;  // Keep running current
        }
        if (idle_process) {
            next = idle_process;
        } else {
            panic("No processes to schedule!");
        }
    }
    
    // Switch to next process
    current_process = next;
    current_process->state = PROCESS_RUNNING;
    current_process->time_slice = time_slices[current_process->priority];
    
    // Switch address space and kernel stack
    switch_address_space(current_process->address_space);
#ifdef ARCH_HAS_SEGMENTATION
    arch_set_kernel_stack(current_process->kernel_stack);
#endif
    
    // Perform context switch if different process
    if (old_process != current_process) {
        switch_context(&old_process->context, &current_process->context);
    }
}

// Memory management - sbrk
void* process_sbrk(int increment) {
    if (!current_process || !current_process->address_space) {
        return (void*)-1;
    }
    
    address_space_t* as = current_process->address_space;
    uintptr_t old_heap = as->heap_end;
    
    if (increment > 0) {
        // Expand heap
        uintptr_t new_heap = old_heap + (uintptr_t)increment;
        if (new_heap < old_heap) {
            return (void*)-1;
        }
        if (vmm_alloc_at(as, old_heap, (size_t)increment, VMM_PRESENT | VMM_WRITE | VMM_USER)) {
            as->heap_end = new_heap;
            return (void*)old_heap;
        }
        return (void*)-1;
    } else if (increment < 0) {
        // Shrink heap
        uintptr_t shrink = (uintptr_t)(-increment);
        if (shrink > (old_heap - as->heap_start)) {
            return (void*)-1;  // Can't shrink below start
        }
        as->heap_end -= shrink;
        vmm_free_pages(as, as->heap_end, (size_t)(shrink / PAGE_SIZE));
        return (void*)as->heap_end;
    }
    
    return (void*)old_heap;
}

// Fork - create copy of current process
int process_fork(void) {
    if (!current_process) {
        return -1;
    }
    
    // Allocate new process
    process_t* child = allocate_process();
    if (!child) {
        return -1;
    }
    
    // Copy basic info
    strcpy(child->name, current_process->name);
    strcat(child->name, "-fork");
    child->task_type = TASK_TYPE_PROCESS;
    child->schedulable = 1;
    child->priority = clamp_priority(current_process->priority);
    child->state = PROCESS_READY;
    child->parent_pid = current_process->pid;
    child->parent = current_process;
    child->time_slice = time_slices[child->priority];
    
    // Create address space (copy-on-write would be ideal, but we'll do full copy)
    child->address_space = create_address_space();
    if (!child->address_space) {
        child->state = PROCESS_DEAD;
        return -1;
    }
    
    // Copy address space (simplified - full copy)
    // In real OS, use copy-on-write
    // TODO: Implement proper memory copying
    
    // Allocate kernel stack
    void* child_kernel_stack_mem = kmalloc(8192);
    if (!child_kernel_stack_mem) {
        destroy_address_space(child->address_space);
        child->address_space = NULL;
        child->state = PROCESS_DEAD;
        return -1;
    }
    child->kernel_stack = (uintptr_t)child_kernel_stack_mem + 8192;
    
    // Copy context (child returns 0, parent returns child PID)
    memcpy(&child->context, &current_process->context, sizeof(cpu_context_t));
    child->context.eax = 0;  // Child returns 0 from fork
    child->context.cr3 = (uintptr_t)child->address_space->page_dir->physical_addr;
    
    // Add to parent's children list
    child->sibling = current_process->children;
    current_process->children = child;
    
    // Add to ready queue
    enqueue_process(child);
    
    return child->pid;  // Parent returns child PID
}

// Wait for child process
int process_waitpid(int pid, int* status, int options) {
    (void)options;  // TODO: Handle options like WNOHANG
    
    if (!current_process) {
        return -1;
    }
    
    // Find child process
    process_t* child = NULL;
    if (pid > 0) {
        // Wait for specific child
        child = process_get_by_pid(pid);
        if (!child || child->parent_pid != current_process->pid) {
            return -1;  // Not our child
        }
    } else {
        // Wait for any child
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].parent_pid == current_process->pid &&
                process_table[i].state != PROCESS_DEAD) {
                child = &process_table[i];
                break;
            }
        }
        if (!child) {
            return -1;  // No children
        }
    }
    
    // If child is zombie, reap it
    if (child->state == PROCESS_ZOMBIE) {
        if (status) {
            *status = child->exit_status;
        }
        pid_t child_pid = child->pid;
        
        // Cleanup child resources
        if (child->address_space) {
            destroy_address_space(child->address_space);
        }
        if (current_process->children_count > 0) {
            current_process->children_count--;
        }
        child->state = PROCESS_DEAD;
        
        return child_pid;
    }
    
    // Block until child exits
    current_process->state = PROCESS_BLOCKED;
    schedule();
    
    // After waking up, check again
    return process_waitpid(pid, status, options);
}

// Kill process
int process_kill(int pid, int signal) {
    (void)signal;

    process_t* proc = process_get_by_pid(pid);
    if (!proc || proc->state == PROCESS_DEAD) {
        return -1;
    }

    if (!proc->schedulable) {
        if (proc->task_type == TASK_TYPE_SERVICE) {
            const char* svc_name = proc->name;
            if (strncmp(proc->name, "svc:", 4) == 0) {
                svc_name = proc->name + 4;
            }
            return init_stop_service(svc_name);
        }

        if (proc->task_type == TASK_TYPE_MODULE) {
            const char* mod_name = proc->name;
            if (strncmp(proc->name, "kmod:", 5) == 0) {
                mod_name = proc->name + 5;
            }

            if (kmodule_unload_v2(mod_name) == 0) {
                return 0;
            }
            return kmodule_unload(mod_name);
        }

        if (proc->task_type == TASK_TYPE_KERNEL ||
            proc->task_type == TASK_TYPE_DRIVER ||
            proc->task_type == TASK_TYPE_SUBSYSTEM) {
            return -1;
        }

        if (proc->parent && proc->parent->children_count > 0) {
            proc->parent->children_count--;
        }
        proc->exit_status = 128 + signal;
        proc->state = PROCESS_DEAD;
        return 0;
    }
    
    // Simple implementation: just exit the process
    if (proc == current_process) {
        process_exit(128 + signal);
    } else {
        proc->exit_status = 128 + signal;
        proc->state = PROCESS_ZOMBIE;
        
        // Wake up parent if waiting
        if (proc->parent && proc->parent->state == PROCESS_BLOCKED) {
            enqueue_process(proc->parent);
        }
    }
    
    return 0;
}

// Execute new program (replaces current process)
int process_execve(const char* path, char* const argv[], char* const envp[]) {
    (void)envp;  // TODO: Pass environment to new program
    
    if (!current_process || !path) {
        return -1;
    }
    
    // Count arguments
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }
    
    // Destroy current address space
    if (current_process->address_space != kernel_address_space) {
        destroy_address_space(current_process->address_space);
    }
    
    // Create new address space
    current_process->address_space = create_address_space();
    if (!current_process->address_space) {
        serial_puts("EXEC: Failed to create address space\n");
        process_exit(-1);
        return -1;
    }
    
    // Load ELF binary
    uintptr_t entry_point;
    if (elf_load(path, &entry_point) != 0) {
        serial_puts("EXEC: Failed to load ELF\n");
        process_exit(-1);
        return -1;
    }
    
    // Setup new user stack
    current_process->user_stack = VMM_USER_STACK_TOP;
    vmm_alloc_at(current_process->address_space, 
                 current_process->user_stack - 8192, 8192,
                 VMM_PRESENT | VMM_WRITE | VMM_USER);
    
    // Switch to new address space
    switch_address_space(current_process->address_space);
    
    // Enter ring 3 and execute the program
    // This function does not return
    extern void enter_usermode(uintptr_t entry_point, uintptr_t user_stack, int argc, char** argv);
    enter_usermode(entry_point, current_process->user_stack, argc, (char**)argv);
    
    // Never reached
    return 0;
}

// Sandbox management implementations (v0.7.3)
int sandbox_apply_to_process(int pid, const sandbox_t* sandbox) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc || !sandbox) {
        return -1;
    }
    
    // Check if current process has permission to modify target
    if (current_process->owner_type != OWNER_SYSTEM && 
        current_process->owner_type != OWNER_ROOT) {
        // Can only modify own processes
        if (proc->pid != current_process->pid && 
            proc->parent_pid != current_process->pid) {
            return -1;
        }
    }
    
    // Check if target sandbox is immutable
    if (proc->sandbox.flags & SANDBOX_IMMUTABLE) {
        return -1;
    }
    
    proc->sandbox = *sandbox;
    return 0;
}

int sandbox_get_from_process(int pid, sandbox_t* sandbox) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc || !sandbox) {
        return -1;
    }
    
    *sandbox = proc->sandbox;
    return 0;
}

int cage_set_root_for_process(int pid, const char* path) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc || !path) {
        return -1;
    }
    
    strncpy(proc->sandbox.cageroot, path, 255);
    proc->sandbox.cageroot[255] = '\0';
    return 0;
}

int cage_get_root_for_process(int pid, char* buffer, size_t size) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc || !buffer || size == 0) {
        return -1;
    }
    
    strncpy(buffer, proc->sandbox.cageroot, size - 1);
    buffer[size - 1] = '\0';
    return 0;
}

int resource_check_memory_for_process(int pid, uint32_t requested) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return 0;
    }
    
    // Check if limit is set (0 = unlimited)
    if (proc->sandbox.limits.max_memory == 0) {
        return 1;
    }
    
    // Check if requested amount would exceed limit
    if (proc->memory_used + requested > proc->sandbox.limits.max_memory) {
        return 0;
    }
    
    return 1;
}

int resource_check_files_for_process(int pid) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return 0;
    }
    
    if (proc->sandbox.limits.max_files == 0) {
        return 1;
    }
    
    return proc->files_open < proc->sandbox.limits.max_files;
}

int resource_check_processes_for_process(int pid) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return 0;
    }
    
    if (proc->sandbox.limits.max_processes == 0) {
        return 1;
    }
    
    return proc->children_count < proc->sandbox.limits.max_processes;
}

int resource_check_time_for_process(int pid) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return 0;
    }
    
    if (proc->sandbox.limits.max_cpu_time == 0) {
        return 1;
    }
    
    return proc->total_time < proc->sandbox.limits.max_cpu_time;
}
