/*
 * === AOS HEADER BEGIN ===
 * src/kernel/kmodule_v2.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/**
 * Kernel Module System v2
 * 
 * Extended module support with capability-based security, 
 * rich API context, and AKM v2 format support.
 */

#include <kmodule.h>
#include <kmodule_api.h>
#include <akm_vm.h>
#include <string.h>
#include <serial.h>
#include <memory.h>
#include <vmm.h>
#include <version.h>
#include <envars.h>
#include <io.h>
#include <command_registry.h>
#include <vga.h>
#include <stdarg.h>
#include <syscall.h>
#include <stdlib.h>
#include <process.h>

// External memory info from kernel.c
extern uint32_t total_memory_kb;

//                        INTERNAL STATE

// V2 module list (separate from v1 for safety)
static kmod_v2_entry_t* v2_module_list = NULL;
static int v2_module_count = 0;
static int v2_initialized = 0;

// Registered IRQ handlers by modules
#define MAX_MODULE_IRQS 16
typedef struct {
    uint8_t irq;
    void (*handler)(void* data);
    void* data;
    kmod_ctx_t* owner;
} mod_irq_entry_t;

static mod_irq_entry_t module_irqs[MAX_MODULE_IRQS];
static int module_irq_count = 0;

// Registered timers
#define MAX_MODULE_TIMERS 32
typedef struct {
    int id;
    uint32_t interval_ms;
    void (*callback)(void* data);
    void* data;
    kmod_ctx_t* owner;
    int active;
} mod_timer_entry_t;

static mod_timer_entry_t module_timers[MAX_MODULE_TIMERS];
static int next_timer_id = 1;

// Module VM command handlers - stores mapping from command name to VM handler
#define MAX_MODULE_COMMANDS 32
typedef struct {
    char cmd_name[64];
    akm_vm_t* vm;
    uint32_t handler_offset;
    uint8_t valid;
    uint8_t _padding[3];
} mod_cmd_entry_t;

static mod_cmd_entry_t module_commands[MAX_MODULE_COMMANDS];
static int module_command_count = 0;

// Internal v2 module entry structure
struct kmod_v2_entry {
    kmodule_t base;              // Embed base structure
    kmod_ctx_t context;          // Module context
    akm_header_v2_t header_v2;   // V2 header copy
    uint32_t capabilities;       // Granted capabilities
    akm_vm_t* vm;                // Bytecode VM (for JS modules)
    int is_bytecode;             // 1 if bytecode module, 0 if native
    struct kmod_v2_entry* next;
};

//                    CAPABILITY CHECK HELPERS

static int check_cap(kmod_ctx_t* ctx, uint32_t cap) {
    if (!ctx) return 0;
    return (ctx->capabilities & cap) != 0;
}

//                      API IMPLEMENTATIONS

// --- Logging ---
static void api_log(kmod_ctx_t* ctx, int level, const char* fmt, ...) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_LOG)) return;
    
    const char* prefix = "";
    switch (level) {
        case KMOD_LOG_EMERG:   prefix = "[EMERG] "; break;
        case KMOD_LOG_ALERT:   prefix = "[ALERT] "; break;
        case KMOD_LOG_CRIT:    prefix = "[CRIT]  "; break;
        case KMOD_LOG_ERR:     prefix = "[ERR]   "; break;
        case KMOD_LOG_WARNING: prefix = "[WARN]  "; break;
        case KMOD_LOG_NOTICE:  prefix = "[NOTE]  "; break;
        case KMOD_LOG_INFO:    prefix = "[INFO]  "; break;
        case KMOD_LOG_DEBUG:   prefix = "[DEBUG] "; break;
    }
    
    serial_puts("[");
    serial_puts(ctx->name);
    serial_puts("] ");
    serial_puts(prefix);
    
    // Handle format string with varargs
    va_list args;
    va_start(args, fmt);
    
    // Simple printf-like formatting (only handles %s for now)
    const char* p = fmt;
    while (*p) {
        if (*p == '%' && *(p+1) == 's') {
            const char* str = va_arg(args, const char*);
            if (str) serial_puts(str);
            p += 2;
        } else {
            char buf[2] = {*p, 0};
            serial_puts(buf);
            p++;
        }
    }
    va_end(args);
    
    serial_puts("\n");
}

static void api_log_hex(kmod_ctx_t* ctx, const void* data, size_t len) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_LOG)) return;
    (void)data; (void)len;
    // TODO: implement hex dump
}

// --- Memory Management ---
static void* api_malloc(kmod_ctx_t* ctx, size_t size) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_MEMORY)) return NULL;
    return kmalloc(size);
}

static void* api_calloc(kmod_ctx_t* ctx, size_t nmemb, size_t size) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_MEMORY)) return NULL;
    void* ptr = kmalloc(nmemb * size);
    if (ptr) memset(ptr, 0, nmemb * size);
    return ptr;
}

static void* api_realloc(kmod_ctx_t* ctx, void* ptr, size_t size) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_MEMORY)) return NULL;
    // Simple realloc: allocate new, copy, free old
    if (!ptr) return api_malloc(ctx, size);
    if (size == 0) { kfree(ptr); return NULL; }
    void* newptr = kmalloc(size);
    if (newptr) {
        memcpy(newptr, ptr, size);  // May copy too much but safe
        kfree(ptr);
    }
    return newptr;
}

static void api_free(kmod_ctx_t* ctx, void* ptr) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_MEMORY)) return;
    if (ptr) kfree(ptr);
}

static void* api_alloc_page(kmod_ctx_t* ctx) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_MEMORY)) return NULL;
    return alloc_page();
}

static void api_free_page(kmod_ctx_t* ctx, void* page) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_MEMORY)) return;
    free_page(page);
}

// Lookup module command by name
static mod_cmd_entry_t* find_module_command(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < MAX_MODULE_COMMANDS; i++) {
        if (module_commands[i].valid && 
            strcmp(module_commands[i].cmd_name, name) == 0) {
            return &module_commands[i];
        }
    }
    return NULL;
}

// Wrapper function for module commands - to be called from command_registry
static void module_cmd_wrapper(const char* args) {
    // Get the command name from the VM context
    // This is tricky - we need to find which command was invoked
    // For now, we'll search through the module commands
    (void)args;
}

// Execute a module VM command
int execute_module_vm_command(const char* cmd_name, const char* args) {
    if (!cmd_name) return -1;
    
    mod_cmd_entry_t* mod_cmd = find_module_command(cmd_name);
    if (!mod_cmd || !mod_cmd->valid) {
        return -1;  // Command not found
    }
    
    if (!mod_cmd->vm) {
        serial_puts("ERROR: Module command VM is NULL\n");
        return -2;
    }
    
    // Reset VM state for this execution
    akm_vm_reset(mod_cmd->vm);
    mod_cmd->vm->pc = mod_cmd->handler_offset;
    mod_cmd->vm->flags = AKM_VM_RUNNING;
    
    // Set the command arguments in the VM so the handler can access them
    mod_cmd->vm->cmd_args = args ? args : "";
    
    int max_instructions = 100000;
    int count = 0;
    
    while (count < max_instructions) {
        int result = akm_vm_step(mod_cmd->vm);
        if (result != 0) {
            break;
        }
        count++;
    }
    
    if (count >= max_instructions) {
        serial_puts("[AKM] Command execution limit exceeded\n");
        return -3;
    }
    
    if (mod_cmd->vm->flags & AKM_VM_ERROR) {
        serial_puts("[AKM] Command execution error\n");
        return mod_cmd->vm->error_code;
    }
    
    return 0;  // Success
}

// --- Commands ---
// Register a module command with VM handler
int register_module_cmd(const char* name, uint32_t handler_offset,
                       akm_vm_t* vm, kmod_ctx_t* ctx) {
    if (!name || !vm || !ctx) return -1;
    
    // Find free slot
    for (int i = 0; i < MAX_MODULE_COMMANDS; i++) {
        if (!module_commands[i].valid) {
            strncpy(module_commands[i].cmd_name, name, 63);
            module_commands[i].cmd_name[63] = '\0';
            module_commands[i].handler_offset = handler_offset;
            module_commands[i].vm = vm;
            module_commands[i].valid = 1;
            module_command_count++;
            return i;  // Return slot index
        }
    }
    return -1;  // No free slots
}

// Unregister all commands for a module (called during cleanup)
static void unregister_module_commands(akm_vm_t* vm) {
    if (!vm) return;
    
    for (int i = 0; i < MAX_MODULE_COMMANDS; i++) {
        if (module_commands[i].valid && module_commands[i].vm == vm) {
            module_commands[i].valid = 0;
            module_commands[i].vm = NULL;
            module_command_count--;
        }
    }
}

static int api_register_command(kmod_ctx_t* ctx, const kmod_command_t* cmd) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_COMMAND)) return KMOD_ERR_CAPABILITY;
    if (!cmd || !cmd->name || !cmd->name[0]) return KMOD_ERR_INVALID;
    
    // Validate name length
    size_t name_len = 0;
    while (cmd->name[name_len] && name_len < 64) name_len++;
    if (name_len == 0 || name_len >= 64) return KMOD_ERR_INVALID;
    
    // Check if this is a module command that was registered with a VM handler
    mod_cmd_entry_t* mod_cmd = find_module_command(cmd->name);
    
    if (mod_cmd && mod_cmd->valid) {
        // This is a VM command - register with a wrapper
        // For now, register with NULL handler since we need the VM wrapper
        // The VM command wrapper will be called through register_module_cmd
        command_register_with_category(cmd->name, cmd->syntax ? cmd->syntax : "", 
                                       cmd->description ? cmd->description : "", 
                                       cmd->category ? cmd->category : "Module", NULL);
    } else {
        // Regular command or no VM handler
        command_register_with_category(cmd->name, cmd->syntax ? cmd->syntax : "", 
                                       cmd->description ? cmd->description : "", 
                                       cmd->category ? cmd->category : "Module", NULL);
    }
    
    serial_puts("[MOD] Registered command: ");
    serial_puts(cmd->name);
    serial_puts("\n");
    
    return KMOD_OK;
}

static int api_unregister_command(kmod_ctx_t* ctx, const char* name) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_COMMAND)) return KMOD_ERR_CAPABILITY;
    if (!name) return KMOD_ERR_INVALID;
    // TODO: Implement unregister in command registry
    (void)name;
    return KMOD_ERR_INVALID;  // Not implemented yet
}

// --- Environment Variables ---
static const char* api_getenv(kmod_ctx_t* ctx, const char* name) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_ENVVAR)) return NULL;
    return envar_get(name);
}

static int api_setenv(kmod_ctx_t* ctx, const char* name, const char* value) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_ENVVAR)) return KMOD_ERR_CAPABILITY;
    return envar_set(name, value);
}

static int api_unsetenv(kmod_ctx_t* ctx, const char* name) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_ENVVAR)) return KMOD_ERR_CAPABILITY;
    return envar_unset(name);
}

// --- I/O Port Access ---
static void api_outb(kmod_ctx_t* ctx, uint16_t port, uint8_t val) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IO_PORT)) return;
    outb(port, val);
}

static void api_outw(kmod_ctx_t* ctx, uint16_t port, uint16_t val) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IO_PORT)) return;
    outw(port, val);
}

static void api_outl(kmod_ctx_t* ctx, uint16_t port, uint32_t val) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IO_PORT)) return;
    outl(port, val);
}

static uint8_t api_inb(kmod_ctx_t* ctx, uint16_t port) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IO_PORT)) return 0;
    return inb(port);
}

static uint16_t api_inw(kmod_ctx_t* ctx, uint16_t port) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IO_PORT)) return 0;
    return inw(port);
}

static uint32_t api_inl(kmod_ctx_t* ctx, uint16_t port) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IO_PORT)) return 0;
    return inl(port);
}

static void api_io_wait(kmod_ctx_t* ctx) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IO_PORT)) return;
    io_wait();
}

// --- Timer Functions ---
static uint32_t api_get_ticks(kmod_ctx_t* ctx) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_TIMER)) return 0;
    // TODO: Return actual tick count from timer system
    return 0;
}

static void api_sleep_ms(kmod_ctx_t* ctx, uint32_t ms) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_TIMER)) return;
    // Simple busy-wait sleep (not ideal but works)
    volatile uint32_t count = ms * 10000;
    while (count-- > 0) {
        __asm__ volatile ("nop");
    }
}

static int api_create_timer(kmod_ctx_t* ctx, uint32_t interval_ms, 
                           void (*callback)(void* data), void* data) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_TIMER)) return KMOD_ERR_CAPABILITY;
    if (!callback) return KMOD_ERR_INVALID;
    
    // Find free slot
    for (int i = 0; i < MAX_MODULE_TIMERS; i++) {
        if (!module_timers[i].active) {
            module_timers[i].id = next_timer_id++;
            module_timers[i].interval_ms = interval_ms;
            module_timers[i].callback = callback;
            module_timers[i].data = data;
            module_timers[i].owner = ctx;
            module_timers[i].active = 1;
            return module_timers[i].id;
        }
    }
    return KMOD_ERR_LIMIT;
}

static int api_start_timer(kmod_ctx_t* ctx, int timer_id) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_TIMER)) return KMOD_ERR_CAPABILITY;
    
    for (int i = 0; i < MAX_MODULE_TIMERS; i++) {
        if (module_timers[i].active && module_timers[i].id == timer_id) {
            if (module_timers[i].owner != ctx) return KMOD_ERR_CAPABILITY;
            // Timer is already active, nothing to do
            return 0;
        }
    }
    return KMOD_ERR_NOTFOUND;
}

static int api_stop_timer(kmod_ctx_t* ctx, int timer_id) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_TIMER)) return KMOD_ERR_CAPABILITY;
    
    for (int i = 0; i < MAX_MODULE_TIMERS; i++) {
        if (module_timers[i].active && module_timers[i].id == timer_id) {
            if (module_timers[i].owner != ctx) return KMOD_ERR_CAPABILITY;
            // Mark as inactive but don't free slot
            module_timers[i].active = 0;
            return 0;
        }
    }
    return KMOD_ERR_NOTFOUND;
}

static void api_destroy_timer(kmod_ctx_t* ctx, int timer_id) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_TIMER)) return;
    
    for (int i = 0; i < MAX_MODULE_TIMERS; i++) {
        if (module_timers[i].active && module_timers[i].id == timer_id) {
            if (module_timers[i].owner != ctx) return;
            module_timers[i].active = 0;
            return;
        }
    }
}

// --- System Info ---
static int api_get_sysinfo(kmod_ctx_t* ctx, kmod_sysinfo_t* info) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_SYSINFO)) return KMOD_ERR_CAPABILITY;
    if (!info) return KMOD_ERR_INVALID;
    
    memset(info, 0, sizeof(kmod_sysinfo_t));
    info->kernel_version = kernel_get_version();
    info->api_version = KMOD_API_VERSION;
    info->total_memory = total_memory_kb * 1024;  // Convert KB to bytes
    info->free_memory = 0;  // TODO: track free memory
    info->cpu_count = 1;  // TODO: detect SMP
    info->module_count = v2_module_count;
    strncpy(info->kernel_name, "aOS", 31);
    strncpy(info->arch, "i386", 15);
    info->uptime_ticks = 0;  // TODO: track uptime
    
    return 0;
}

static uint32_t api_get_kernel_version(kmod_ctx_t* ctx) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_SYSINFO)) return 0;
    return kernel_get_version();
}

// --- IRQ Management ---
static int api_register_irq(kmod_ctx_t* ctx, uint8_t irq, 
                           void (*handler)(void* data), void* data) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IRQ)) return KMOD_ERR_CAPABILITY;
    if (irq > 15) return KMOD_ERR_INVALID;
    if (module_irq_count >= MAX_MODULE_IRQS) return KMOD_ERR_MEMORY;
    
    module_irqs[module_irq_count].irq = irq;
    module_irqs[module_irq_count].handler = handler;
    module_irqs[module_irq_count].data = data;
    module_irqs[module_irq_count].owner = ctx;
    module_irq_count++;
    
    return 0;
}

static int api_unregister_irq(kmod_ctx_t* ctx, uint8_t irq) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IRQ)) return KMOD_ERR_CAPABILITY;
    
    for (int i = 0; i < module_irq_count; i++) {
        if (module_irqs[i].irq == irq && module_irqs[i].owner == ctx) {
            // Shift remaining entries
            for (int j = i; j < module_irq_count - 1; j++) {
                module_irqs[j] = module_irqs[j + 1];
            }
            module_irq_count--;
            return 0;
        }
    }
    return KMOD_ERR_NOTFOUND;
}

static void api_enable_irq(kmod_ctx_t* ctx, uint8_t irq) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IRQ)) return;
    (void)irq;
    // TODO: unmask IRQ in PIC
}

static void api_disable_irq(kmod_ctx_t* ctx, uint8_t irq) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_IRQ)) return;
    (void)irq;
    // TODO: mask IRQ in PIC
}

// --- VFS stubs ---
static int api_vfs_open(kmod_ctx_t* ctx, const char* path, uint32_t flags) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_FILESYSTEM)) return -1;
    if (!path) return -1;
    return sys_open(path, flags);
}

static int api_vfs_close(kmod_ctx_t* ctx, int fd) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_FILESYSTEM)) return -1;
    return sys_close(fd);
}

static int api_vfs_read(kmod_ctx_t* ctx, int fd, void* buf, size_t size) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_FILESYSTEM)) return -1;
    if (!buf) return -1;
    return sys_read(fd, buf, size);
}

static int api_vfs_write(kmod_ctx_t* ctx, int fd, const void* buf, size_t size) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_FILESYSTEM)) return -1;
    if (!buf) return -1;
    return sys_write(fd, buf, size);
}

static int api_vfs_seek(kmod_ctx_t* ctx, int fd, int32_t offset, int whence) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_FILESYSTEM)) return -1;
    return sys_lseek(fd, offset, whence);
}

// --- Process stubs ---
static int api_spawn(kmod_ctx_t* ctx, const char* name, void (*entry)(void), int priority) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PROCESS)) return -1;
    (void)name; (void)entry; (void)priority;
    return -1;  // TODO: implement
}

static int api_kill(kmod_ctx_t* ctx, int pid, int signal) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PROCESS)) return -1;
    (void)pid; (void)signal;
    return -1;  // TODO: implement
}

static int api_getpid(kmod_ctx_t* ctx) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PROCESS)) return -1;
    return 0;  // Kernel context has PID 0
}

static void api_yield(kmod_ctx_t* ctx) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PROCESS)) return;
    // TODO: implement scheduler yield
}

// --- PCI stubs ---
static kmod_pci_device_t* api_pci_find_device(kmod_ctx_t* ctx, uint16_t vendor, uint16_t device) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PCI)) return NULL;
    (void)vendor; (void)device;
    return NULL;  // TODO: implement
}

static kmod_pci_device_t* api_pci_find_class(kmod_ctx_t* ctx, uint8_t class_code, uint8_t subclass) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PCI)) return NULL;
    (void)class_code; (void)subclass;
    return NULL;  // TODO: implement
}

static uint32_t api_pci_read_config(kmod_ctx_t* ctx, kmod_pci_device_t* dev, uint8_t offset) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PCI)) return 0xFFFFFFFF;
    (void)dev; (void)offset;
    return 0xFFFFFFFF;  // TODO: implement
}

static void api_pci_write_config(kmod_ctx_t* ctx, kmod_pci_device_t* dev, 
                                 uint8_t offset, uint32_t val) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PCI)) return;
    (void)dev; (void)offset; (void)val;
    // TODO: implement
}

static void api_pci_enable_busmaster(kmod_ctx_t* ctx, kmod_pci_device_t* dev) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_PCI)) return;
    (void)dev;
    // TODO: implement
}

// --- Crypto stubs ---
static void api_sha256(kmod_ctx_t* ctx, const void* data, size_t len, uint8_t* hash) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_CRYPTO)) return;
    (void)data; (void)len; (void)hash;
    // TODO: implement using crypto/sha256.h
}

static int api_random_bytes(kmod_ctx_t* ctx, void* buf, size_t len) {
    if (!ctx || !check_cap(ctx, KMOD_CAP_CRYPTO)) return -1;
    // Simple PRNG - not cryptographically secure
    uint8_t* p = (uint8_t*)buf;
    static uint32_t seed = 0x12345678;
    for (size_t i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        p[i] = (seed >> 16) & 0xFF;
    }
    return 0;
}

//                    CONTEXT INITIALIZATION

// Storage for module names (simple approach)
#define MAX_NAME_STORAGE 16
static char name_storage[MAX_NAME_STORAGE][64];
static int name_storage_idx = 0;

static void init_module_context(kmod_ctx_t* ctx, const char* name, uint32_t caps) {
    memset(ctx, 0, sizeof(kmod_ctx_t));
    
    // Store name in static storage since ctx->name is const
    if (name_storage_idx < MAX_NAME_STORAGE) {
        strncpy(name_storage[name_storage_idx], name, 63);
        ctx->name = name_storage[name_storage_idx];
        name_storage_idx++;
    } else {
        ctx->name = "unknown";
    }
    
    ctx->capabilities = caps;
    ctx->api_version = KMOD_API_VERSION;
    
    // Wire up all API functions
    ctx->log = api_log;
    ctx->log_hex = api_log_hex;
    
    ctx->malloc = api_malloc;
    ctx->calloc = api_calloc;
    ctx->realloc = api_realloc;
    ctx->free = api_free;
    ctx->alloc_page = api_alloc_page;
    ctx->free_page = api_free_page;
    
    ctx->register_command = api_register_command;
    ctx->unregister_command = api_unregister_command;
    
    ctx->getenv = api_getenv;
    ctx->setenv = api_setenv;
    ctx->unsetenv = api_unsetenv;
    
    ctx->outb = api_outb;
    ctx->outw = api_outw;
    ctx->outl = api_outl;
    ctx->inb = api_inb;
    ctx->inw = api_inw;
    ctx->inl = api_inl;
    ctx->io_wait = api_io_wait;
    
    ctx->pci_find_device = api_pci_find_device;
    ctx->pci_find_class = api_pci_find_class;
    ctx->pci_read_config = api_pci_read_config;
    ctx->pci_write_config = api_pci_write_config;
    ctx->pci_enable_busmaster = api_pci_enable_busmaster;
    
    ctx->get_ticks = api_get_ticks;
    ctx->sleep_ms = api_sleep_ms;
    ctx->create_timer = api_create_timer;
    ctx->start_timer = api_start_timer;
    ctx->stop_timer = api_stop_timer;
    ctx->destroy_timer = api_destroy_timer;
    
    ctx->get_sysinfo = api_get_sysinfo;
    ctx->get_kernel_version = api_get_kernel_version;
    
    ctx->vfs_open = api_vfs_open;
    ctx->vfs_close = api_vfs_close;
    ctx->vfs_read = api_vfs_read;
    ctx->vfs_write = api_vfs_write;
    ctx->vfs_seek = api_vfs_seek;
    
    ctx->register_irq = api_register_irq;
    ctx->unregister_irq = api_unregister_irq;
    ctx->enable_irq = api_enable_irq;
    ctx->disable_irq = api_disable_irq;
    
    ctx->spawn = api_spawn;
    ctx->kill = api_kill;
    ctx->getpid = api_getpid;
    ctx->yield = api_yield;
    
    ctx->sha256 = api_sha256;
    ctx->random_bytes = api_random_bytes;
}

//                       V2 INITIALIZATION

void init_kmodules_v2(void) {
    if (v2_initialized) return;
    
    serial_puts("Initializing kernel module system v2...\n");
    
    // Initialize module list
    v2_module_list = NULL;
    v2_module_count = 0;
    
    // Initialize timer slots
    memset(module_timers, 0, sizeof(module_timers));
    
    // Initialize IRQ slots
    memset(module_irqs, 0, sizeof(module_irqs));
    module_irq_count = 0;
    
    // Initialize command table
    memset(module_commands, 0, sizeof(module_commands));
    module_command_count = 0;
    
    // Reset name storage
    name_storage_idx = 0;
    
    v2_initialized = 1;
    
    serial_puts("Kernel module v2 system ready (API v");
    // Print API version (major.minor)
    char vbuf[16];
    int major = KMOD_API_VERSION_MAJOR;
    int minor = KMOD_API_VERSION_MINOR;
    vbuf[0] = '0' + major;
    vbuf[1] = '.';
    vbuf[2] = '0' + minor;
    vbuf[3] = '\0';
    serial_puts(vbuf);
    serial_puts(")\n");
}

//                      V2 MODULE LOADING

/**
 * Check if module data has v2 magic
 */
int kmodule_is_v2(const void* data, size_t len) {
    if (!data || len < sizeof(akm_header_v2_t)) return 0;
    
    const akm_header_v2_t* hdr = (const akm_header_v2_t*)data;
    return (hdr->magic == AKM_MAGIC_V2);
}

/**
 * Load a v2 kernel module from memory
 */
int kmodule_load_v2(const void* data, size_t len) {
    if (!v2_initialized) init_kmodules_v2();
    
    if (!data || len < sizeof(akm_header_v2_t)) {
        serial_puts("Error: Invalid module data (NULL or too small)\n");
        return KMOD_ERR_INVALID;
    }
    
    const akm_header_v2_t* hdr = (const akm_header_v2_t*)data;
    
    // Validate magic
    if (hdr->magic != AKM_MAGIC_V2) {
        serial_puts("Error: Invalid v2 module magic (expected 0x324D4B41, got 0x");
        char hex[9];
        for (int i = 7; i >= 0; i--) {
            int nibble = (hdr->magic >> (i * 4)) & 0xF;
            hex[7-i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        }
        hex[8] = '\0';
        serial_puts(hex);
        serial_puts(")\n");
        return KMOD_ERR_INVALID;
    }
    
    // Check format version
    if (hdr->format_version < 2) {
        serial_puts("Error: Unsupported format version\n");
        return KMOD_ERR_VERSION;
    }
    
    // Validate size
    size_t expected_size = sizeof(akm_header_v2_t) + hdr->code_size + 
                          hdr->data_size + hdr->rodata_size + hdr->bss_size;
    if (len < expected_size) {
        serial_puts("Error: Module data truncated (expected ");
        char buf[16];
        itoa(expected_size, buf, 10);
        serial_puts(buf);
        serial_puts(" bytes, got ");
        itoa(len, buf, 10);
        serial_puts(buf);
        serial_puts(")\n");
        return KMOD_ERR_INVALID;
    }
    
    // Check kernel version compatibility
    if (kmodule_check_version(hdr->kernel_min_version) != 0) {
        serial_puts("Error: Module requires newer kernel version\n");
        return KMOD_ERR_VERSION;
    }
    
    // Check if already loaded
    kmod_v2_entry_t* existing = v2_module_list;
    while (existing) {
        if (strcmp(existing->base.name, hdr->name) == 0) {
            serial_puts("Error: Module '");
            serial_puts(hdr->name);
            serial_puts("' already loaded\n");
            return KMOD_ERR_LOADED;
        }
        existing = existing->next;
    }
    
    serial_puts("Loading v2 module: ");
    serial_puts(hdr->name);
    serial_puts("\n");
    
    // Allocate module entry
    kmod_v2_entry_t* entry = (kmod_v2_entry_t*)kmalloc(sizeof(kmod_v2_entry_t));
    if (!entry) {
        serial_puts("Error: Failed to allocate module entry\n");
        return KMOD_ERR_MEMORY;
    }
    
    memset(entry, 0, sizeof(kmod_v2_entry_t));
    
    // Copy header
    memcpy(&entry->header_v2, hdr, sizeof(akm_header_v2_t));
    
    // Setup base module info
    strncpy(entry->base.name, hdr->name, MODULE_NAME_LEN - 1);
    strncpy(entry->base.version, hdr->version, MODULE_VERSION_LEN - 1);
    entry->base.state = MODULE_LOADING;
    entry->base.code_size = hdr->code_size;
    entry->base.data_size = hdr->data_size;
    entry->capabilities = hdr->capabilities;
    
    // Check if this is a bytecode module (FLAG_NATIVE = 0x0002 not set)
    entry->is_bytecode = !(hdr->flags & AKM_FLAG_NATIVE);
    
    // Allocate code section
    if (hdr->code_size > 0) {
        entry->base.code_base = kmalloc(hdr->code_size);
        if (!entry->base.code_base) {
            kfree(entry);
            return KMOD_ERR_MEMORY;
        }
        memcpy(entry->base.code_base, (char*)data + hdr->code_offset, hdr->code_size);
    }
    
    // Allocate data section
    if (hdr->data_size > 0) {
        entry->base.data_base = kmalloc(hdr->data_size);
        if (!entry->base.data_base) {
            if (entry->base.code_base) kfree(entry->base.code_base);
            kfree(entry);
            return KMOD_ERR_MEMORY;
        }
        memcpy(entry->base.data_base, (char*)data + hdr->data_offset, hdr->data_size);
    }
    
    // Initialize context with capabilities
    init_module_context(&entry->context, hdr->name, hdr->capabilities);
    
    // Get string table info for bytecode modules
    const char* strtab = NULL;
    size_t strtab_size = 0;
    if (hdr->strtab_offset > 0 && hdr->strtab_size > 0) {
        strtab = (const char*)data + hdr->strtab_offset;
        strtab_size = hdr->strtab_size;
    }
    
    int result = 0;
    
    if (entry->is_bytecode) {
        // Bytecode module - use VM
        serial_puts("  (bytecode module, using VM)\n");
        
        entry->vm = (akm_vm_t*)kmalloc(sizeof(akm_vm_t));
        if (!entry->vm) {
            if (entry->base.data_base) kfree(entry->base.data_base);
            if (entry->base.code_base) kfree(entry->base.code_base);
            kfree(entry);
            return KMOD_ERR_MEMORY;
        }
        
        akm_vm_init(entry->vm, 
                   entry->base.code_base, hdr->code_size,
                   entry->base.data_base, hdr->data_size,
                   strtab, strtab_size,
                   &entry->context);
        
        // Execute init function via VM
        result = akm_vm_execute(entry->vm, hdr->init_offset);
        
        if (result < 0) {
            serial_puts("Error: Module init (bytecode) failed\n");
            kfree(entry->vm);
            if (entry->base.data_base) kfree(entry->base.data_base);
            if (entry->base.code_base) kfree(entry->base.code_base);
            kfree(entry);
            return result;
        }
    } else {
        // Native module - call init directly
        serial_puts("  (native module)\n");
        
        entry->base.init = (module_init_fn)((char*)entry->base.code_base + hdr->init_offset);
        entry->base.cleanup = (module_cleanup_fn)((char*)entry->base.code_base + hdr->cleanup_offset);
        
        if (entry->base.init) {
            // Use proper function pointer cast to avoid -Wcast-function-type
            kmod_init_fn v2_init;
            *(void**)(&v2_init) = (void*)entry->base.init;
            result = v2_init(&entry->context);
            
            if (result != 0) {
                serial_puts("Error: Module init failed with code ");
                char ebuf[8];
                ebuf[0] = '0' + (result % 10);
                ebuf[1] = '\0';
                serial_puts(ebuf);
                serial_puts("\n");
                
                if (entry->base.data_base) kfree(entry->base.data_base);
                if (entry->base.code_base) kfree(entry->base.code_base);
                kfree(entry);
                return result;
            }
        }
    }
    
    // Add to list
    char task_name[MODULE_NAME_LEN + 6];
    snprintf(task_name, sizeof(task_name), "kmod:%s", entry->base.name);
    pid_t task_id = process_register_kernel_task(task_name, TASK_TYPE_MODULE, PRIORITY_HIGH);
    if (task_id > 0) {
        entry->base.task_id = (uint32_t)task_id;
    }

    entry->base.state = MODULE_LOADED;
    entry->next = v2_module_list;
    v2_module_list = entry;
    v2_module_count++;
    
    serial_puts("Module loaded: ");
    serial_puts(entry->base.name);
    serial_puts(" v");
    serial_puts(entry->base.version);
    serial_puts(" (caps: 0x");
    // Print caps in hex
    char cbuf[12];
    uint32_t c = entry->capabilities;
    for (int i = 7; i >= 0; i--) {
        int nibble = (c >> (i * 4)) & 0xF;
        cbuf[7-i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
    }
    cbuf[8] = '\0';
    serial_puts(cbuf);
    serial_puts(")\n");
    
    return 0;
}

// Cleanup all resources owned by a module (called during unload)
static void cleanup_module_resources(kmod_ctx_t* ctx, akm_vm_t* vm) {
    if (!ctx) return;
    
    // Cleanup timers
    for (int i = 0; i < MAX_MODULE_TIMERS; i++) {
        if (module_timers[i].active && module_timers[i].owner == ctx) {
            module_timers[i].active = 0;
        }
    }
    
    // Cleanup IRQs
    for (int i = 0; i < module_irq_count; ) {
        if (module_irqs[i].owner == ctx) {
            // Shift remaining entries
            for (int j = i; j < module_irq_count - 1; j++) {
                module_irqs[j] = module_irqs[j + 1];
            }
            module_irq_count--;
        } else {
            i++;
        }
    }
    
    // Cleanup VM registry and commands
    if (vm) {
        akm_vm_cleanup_registry(vm);
        unregister_module_commands(vm);
    }
}

/**
 * Unload a v2 module by name
 */
int kmodule_unload_v2(const char* name) {
    if (!name) return KMOD_ERR_INVALID;
    
    kmod_v2_entry_t** prev = &v2_module_list;
    kmod_v2_entry_t* current = v2_module_list;
    
    while (current) {
        if (strcmp(current->base.name, name) == 0) {
            // Found it
            current->base.state = MODULE_UNLOADING;
            
            serial_puts("Cleaning up module resources for '");
            serial_puts(name);
            serial_puts("'...\n");
            
            // Cleanup all module resources first
            cleanup_module_resources(&current->context, current->vm);
            
            // Call cleanup - handle bytecode vs native
            if (current->is_bytecode && current->vm) {
                // Execute cleanup via VM
                serial_puts("Executing bytecode cleanup...\n");
                akm_vm_execute(current->vm, current->header_v2.cleanup_offset);
            } else if (current->base.cleanup) {
                // For v2 native, cleanup takes context
                serial_puts("Calling native cleanup...\n");
                kmod_exit_fn v2_exit = (kmod_exit_fn)current->base.cleanup;
                v2_exit(&current->context);
            }
            
            // Unlink from list
            *prev = current->next;
            
            // Free resources
            if (current->base.task_id != 0) {
                process_finish_kernel_task((pid_t)current->base.task_id, 0);
            }
            if (current->vm) kfree(current->vm);
            if (current->base.data_base) kfree(current->base.data_base);
            if (current->base.code_base) kfree(current->base.code_base);
            kfree(current);
            
            v2_module_count--;
            
            serial_puts("Module unloaded: ");
            serial_puts(name);
            serial_puts("\n");
            
            return 0;
        }
        
        prev = &current->next;
        current = current->next;
    }
    
    serial_puts("Error: Module not found: ");
    serial_puts(name);
    serial_puts("\n");
    
    return KMOD_ERR_NOTFOUND;
}

/**
 * List all v2 modules
 */
void kmodule_list_v2(void) {
    serial_puts("=== Kernel Modules (v2) ===\n");
    
    kmod_v2_entry_t* current = v2_module_list;
    while (current) {
        serial_puts("  ");
        serial_puts(current->base.name);
        serial_puts(" v");
        serial_puts(current->base.version);
        
        switch (current->base.state) {
            case MODULE_LOADED:    serial_puts(" [LOADED]"); break;
            case MODULE_LOADING:   serial_puts(" [LOADING]"); break;
            case MODULE_UNLOADING: serial_puts(" [UNLOADING]"); break;
            case MODULE_ERROR:     serial_puts(" [ERROR]"); break;
            default:               serial_puts(" [???]"); break;
        }
        
        // Print capabilities summary
        serial_puts(" caps=0x");
        char cbuf[12];
        uint32_t c = current->capabilities;
        for (int i = 7; i >= 0; i--) {
            int nibble = (c >> (i * 4)) & 0xF;
            cbuf[7-i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        }
        cbuf[8] = '\0';
        serial_puts(cbuf);
        if (current->base.task_id != 0) {
            serial_puts(" tid=");
            char tid_buf[12];
            itoa((int)current->base.task_id, tid_buf, 10);
            serial_puts(tid_buf);
        }
        
        serial_puts("\n");
        current = current->next;
    }
    
    if (v2_module_count == 0) {
        serial_puts("  (no v2 modules loaded)\n");
    }
}

/**
 * Find a v2 module by name
 */
kmod_ctx_t* kmodule_get_context(const char* name) {
    if (!name) return NULL;
    
    kmod_v2_entry_t* current = v2_module_list;
    while (current) {
        if (strcmp(current->base.name, name) == 0) {
            return &current->context;
        }
        current = current->next;
    }
    
    return NULL;
}

/**
 * Get v2 module count
 */
int kmodule_count_v2(void) {
    return v2_module_count;
}
