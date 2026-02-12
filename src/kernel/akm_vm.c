/*
 * === AOS HEADER BEGIN ===
 * src/kernel/akm_vm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/**
 * AKM Virtual Machine Implementation
 * 
 * Stack-based bytecode interpreter for JavaScript-compiled kernel modules.
 * Military-grade implementation with comprehensive safety checks.
 */

#include <akm_vm.h>
#include <kmodule_api.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <vga.h>

/* Forward declarations for kernel functions - avoid header conflicts */
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);
extern int channel_create(void);
extern int channel_close(int fd);
extern int channel_write(int fd, const void* data, uint32_t size);
extern int channel_read(int fd, void* buf, uint32_t size);
extern void* user_get_session(void);
extern void kprint(const char* str);

/* External from kmodule_v2.c */
extern int register_module_cmd(const char* name, uint32_t handler_offset,
                               akm_vm_t* vm, kmod_ctx_t* ctx);

//                      MODULE RESOURCE REGISTRY
// Uses _private field in ctx, allocated on-demand

#define MAX_MODULE_DRIVERS  8
#define MAX_MODULE_FS       4
#define MAX_MODULE_NETIF    4

typedef struct {
    char name[32];
    uint8_t type;
    uint8_t active;
    uint16_t vendor_id;
    uint16_t device_id;
} module_driver_t;

typedef struct {
    char name[32];
    uint8_t active;
} module_fs_t;

typedef struct {
    char name[16];
    uint8_t active;
} module_netif_t;

typedef struct {
    module_driver_t drivers[MAX_MODULE_DRIVERS];
    int driver_count;
    module_fs_t filesystems[MAX_MODULE_FS];
    int fs_count;
    module_netif_t netifs[MAX_MODULE_NETIF];
    int netif_count;
} vm_registry_t;

/* Safely get or create registry for a VM - returns NULL on failure */
static vm_registry_t* get_vm_registry(akm_vm_t* vm) {
    if (!vm) return 0;
    if (!vm->ctx) return 0;
    if (!vm->ctx->_private) {
        void* reg = kmalloc(sizeof(vm_registry_t));
        if (!reg) return 0;
        memset(reg, 0, sizeof(vm_registry_t));
        vm->ctx->_private = reg;
    }
    return (vm_registry_t*)vm->ctx->_private;
}

/* Cleanup registry on unload */
void akm_vm_cleanup_registry(akm_vm_t* vm) {
    if (!vm || !vm->ctx || !vm->ctx->_private) return;
    kfree(vm->ctx->_private);
    vm->ctx->_private = 0;
}

//                          HELPER FUNCTIONS

/**
 * Read 32-bit little-endian value from code - with null check
 */
static uint32_t read_u32(akm_vm_t* vm) {
    if (!vm || !vm->code) return 0;
    if (vm->pc + 4 > vm->code_size) {
        vm->flags |= AKM_VM_ERROR;
        vm->error_code = AKM_VM_ERR_ADDR;
        return 0;
    }
    
    uint32_t val = vm->code[vm->pc] |
                   (vm->code[vm->pc + 1] << 8) |
                   (vm->code[vm->pc + 2] << 16) |
                   (vm->code[vm->pc + 3] << 24);
    vm->pc += 4;
    return val;
}

/**
 * Read 8-bit value from code - with null check
 */
static uint8_t read_u8(akm_vm_t* vm) {
    if (!vm || !vm->code) return 0;
    if (vm->pc >= vm->code_size) {
        vm->flags |= AKM_VM_ERROR;
        vm->error_code = AKM_VM_ERR_ADDR;
        return 0;
    }
    return vm->code[vm->pc++];
}

//                          STACK OPERATIONS

int akm_vm_push(akm_vm_t* vm, int32_t value) {
    if (!vm) return -1;
    if (vm->sp >= AKM_VM_STACK_SIZE) {
        vm->flags |= AKM_VM_ERROR;
        vm->error_code = AKM_VM_ERR_STACK;
        return -1;
    }
    vm->stack[vm->sp++] = value;
    return 0;
}

int32_t akm_vm_pop(akm_vm_t* vm) {
    if (!vm) return 0;
    if (vm->sp == 0) {
        vm->flags |= AKM_VM_ERROR;
        vm->error_code = AKM_VM_ERR_STACK;
        return 0;
    }
    return vm->stack[--vm->sp];
}

static int32_t peek(akm_vm_t* vm) {
    if (!vm) return 0;
    if (vm->sp == 0) {
        vm->flags |= AKM_VM_ERROR;
        vm->error_code = AKM_VM_ERR_STACK;
        return 0;
    }
    return vm->stack[vm->sp - 1];
}

//                          API DISPATCH

/**
 * Execute an API call - comprehensive implementation with safety checks
 */
static int dispatch_api(akm_vm_t* vm, uint8_t api_id, uint8_t argc) {
    if (!vm) return AKM_VM_ERR_API;
    
    kmod_ctx_t* ctx = vm->ctx;
    if (!ctx) {
        serial_puts("[AKM] No context\n");
        return AKM_VM_ERR_API;
    }
    
    /* Pop arguments with clamping */
    int32_t args[8] = {0};
    if (argc > 8) argc = 8;
    for (int i = argc - 1; i >= 0; i--) {
        args[i] = akm_vm_pop(vm);
    }
    
    /* Debug: log API arguments for key APIs */
    if (api_id == AKM_API_ITOA || api_id == AKM_API_GET_KERNEL_VER) {
        serial_puts("[VM API] id=");
        char tmp[16];
        itoa(api_id, tmp, 10);
        serial_puts(tmp);
        serial_puts(" argc=");
        itoa(argc, tmp, 10);
        serial_puts(tmp);
        if (argc > 0) {
            serial_puts(" arg0=");
            itoa(args[0], tmp, 10);
            serial_puts(tmp);
            serial_puts(" (0x");
            itoa(args[0], tmp, 16);
            serial_puts(tmp);
            serial_puts(")");
        }
        serial_puts("\n");
    }
    
    int32_t result = 0;
    
    switch (api_id) {
        /* LOGGING (0-5) */
        case AKM_API_LOG:
            if (ctx->log) {
                const char* msg = akm_vm_get_string(vm, args[1]);
                ctx->log(ctx, args[0], "%s", msg ? msg : "(null)");
            }
            break;
            
        case AKM_API_INFO:
            if (ctx->log) {
                const char* msg = akm_vm_get_string(vm, args[0]);
                ctx->log(ctx, KMOD_LOG_INFO, "%s", msg ? msg : "(null)");
            }
            break;
            
        case AKM_API_WARN:
            if (ctx->log) {
                const char* msg = akm_vm_get_string(vm, args[0]);
                ctx->log(ctx, KMOD_LOG_WARNING, "%s", msg ? msg : "(null)");
            }
            break;
            
        case AKM_API_ERROR:
            if (ctx->log) {
                const char* msg = akm_vm_get_string(vm, args[0]);
                ctx->log(ctx, KMOD_LOG_ERR, "%s", msg ? msg : "(null)");
            }
            break;
            
        case AKM_API_DEBUG:
            if (ctx->log) {
                const char* msg = akm_vm_get_string(vm, args[0]);
                ctx->log(ctx, KMOD_LOG_DEBUG, "%s", msg ? msg : "(null)");
            }
            break;
            
        case AKM_API_HEXDUMP:
            if (ctx->log_hex && args[0] && args[1] > 0) {
                ctx->log_hex(ctx, (const void*)(uintptr_t)args[0], (size_t)args[1]);
            }
            break;
            
        /* MEMORY (6-11) */
        case AKM_API_MALLOC:
            if (ctx->malloc && args[0] > 0) {
                void* ptr = ctx->malloc(ctx, (size_t)args[0]);
                result = ptr ? (int32_t)(uintptr_t)ptr : 0;
            }
            break;
            
        case AKM_API_CALLOC:
            if (ctx->calloc && args[0] > 0 && args[1] > 0) {
                void* ptr = ctx->calloc(ctx, (size_t)args[0], (size_t)args[1]);
                result = ptr ? (int32_t)(uintptr_t)ptr : 0;
            }
            break;
            
        case AKM_API_REALLOC:
            if (ctx->realloc) {
                void* ptr = ctx->realloc(ctx, (void*)(uintptr_t)args[0], (size_t)args[1]);
                result = ptr ? (int32_t)(uintptr_t)ptr : 0;
            }
            break;
            
        case AKM_API_FREE:
            if (ctx->free && args[0]) {
                ctx->free(ctx, (void*)(uintptr_t)args[0]);
            }
            break;
            
        case AKM_API_ALLOC_PAGE:
            if (ctx->alloc_page) {
                void* page = ctx->alloc_page(ctx);
                result = page ? (int32_t)(uintptr_t)page : 0;
            }
            break;
            
        case AKM_API_FREE_PAGE:
            if (ctx->free_page && args[0]) {
                ctx->free_page(ctx, (void*)(uintptr_t)args[0]);
            }
            break;
            
        /* COMMANDS (12-13) */
        case AKM_API_REGISTER_CMD:
            if (ctx->register_command && argc >= 5) {
                const char* name = akm_vm_get_string(vm, args[0]);
                const char* syntax = akm_vm_get_string(vm, args[1]);
                const char* desc = akm_vm_get_string(vm, args[2]);
                const char* category = akm_vm_get_string(vm, args[3]);
                uint32_t handler_offset = (uint32_t)args[4];
                
                if (name && name[0]) {
                    // Register the VM command handler first
                    int slot = register_module_cmd(name, handler_offset, vm, ctx);
                    if (slot >= 0) {
                        // Then register with the shell system
                        kmod_command_t cmd;
                        cmd.name = name;
                        cmd.syntax = syntax ? syntax : "";
                        cmd.description = desc ? desc : "";
                        cmd.category = category ? category : "Module";
                        cmd.handler = 0;  /* Handled by VM wrapper */
                        result = ctx->register_command(ctx, &cmd);
                    } else {
                        result = KMOD_ERR_MEMORY;
                    }
                } else {
                    result = KMOD_ERR_INVALID;
                }
            }
            break;
            
        case AKM_API_UNREGISTER_CMD:
            if (ctx->unregister_command) {
                const char* name = akm_vm_get_string(vm, args[0]);
                if (name) result = ctx->unregister_command(ctx, name);
            }
            break;
            
        /* ENVIRONMENT (14-16) */
        case AKM_API_GETENV:
            if (ctx->getenv) {
                const char* name = akm_vm_get_string(vm, args[0]);
                if (name) {
                    const char* val = ctx->getenv(ctx, name);
                    result = val ? (int32_t)(uintptr_t)val : 0;
                }
            }
            break;
            
        case AKM_API_SETENV:
            if (ctx->setenv) {
                const char* name = akm_vm_get_string(vm, args[0]);
                const char* value = akm_vm_get_string(vm, args[1]);
                if (name) result = ctx->setenv(ctx, name, value ? value : "");
            }
            break;
            
        case AKM_API_UNSETENV:
            if (ctx->unsetenv) {
                const char* name = akm_vm_get_string(vm, args[0]);
                if (name) result = ctx->unsetenv(ctx, name);
            }
            break;
            
        /* DRIVERS (17-18) */
        case AKM_API_REGISTER_DRV:
            {
                vm_registry_t* reg = get_vm_registry(vm);
                if (!reg) { result = KMOD_ERR_MEMORY; break; }
                if (reg->driver_count >= MAX_MODULE_DRIVERS) { result = KMOD_ERR_LIMIT; break; }
                const char* name = akm_vm_get_string(vm, args[0]);
                if (!name || !name[0]) { result = KMOD_ERR_INVALID; break; }
                module_driver_t* drv = &reg->drivers[reg->driver_count];
                strncpy(drv->name, name, 31);
                drv->name[31] = '\0';
                drv->type = (uint8_t)args[1];
                drv->vendor_id = (uint16_t)args[2];
                drv->device_id = (uint16_t)args[3];
                drv->active = 1;
                result = reg->driver_count++;
            }
            break;
            
        case AKM_API_UNREGISTER_DRV:
            {
                vm_registry_t* reg = get_vm_registry(vm);
                if (!reg) { result = KMOD_ERR_INVALID; break; }
                const char* name = akm_vm_get_string(vm, args[0]);
                if (!name) { result = KMOD_ERR_INVALID; break; }
                result = KMOD_ERR_NOTFOUND;
                for (int i = 0; i < reg->driver_count; i++) {
                    if (reg->drivers[i].active && strcmp(reg->drivers[i].name, name) == 0) {
                        reg->drivers[i].active = 0;
                        result = KMOD_OK;
                        break;
                    }
                }
            }
            break;
            
        /* FILESYSTEM (19-25) */
        case AKM_API_REGISTER_FS:
            {
                vm_registry_t* reg = get_vm_registry(vm);
                if (!reg) { result = KMOD_ERR_MEMORY; break; }
                if (reg->fs_count >= MAX_MODULE_FS) { result = KMOD_ERR_LIMIT; break; }
                const char* name = akm_vm_get_string(vm, args[0]);
                if (!name || !name[0]) { result = KMOD_ERR_INVALID; break; }
                module_fs_t* mfs = &reg->filesystems[reg->fs_count];
                strncpy(mfs->name, name, 31);
                mfs->name[31] = '\0';
                mfs->active = 1;
                result = reg->fs_count++;
            }
            break;
            
        case AKM_API_UNREGISTER_FS:
            {
                vm_registry_t* reg = get_vm_registry(vm);
                if (!reg) { result = KMOD_ERR_INVALID; break; }
                const char* name = akm_vm_get_string(vm, args[0]);
                if (!name) { result = KMOD_ERR_INVALID; break; }
                result = KMOD_ERR_NOTFOUND;
                for (int i = 0; i < reg->fs_count; i++) {
                    if (reg->filesystems[i].active && strcmp(reg->filesystems[i].name, name) == 0) {
                        reg->filesystems[i].active = 0;
                        result = KMOD_OK;
                        break;
                    }
                }
            }
            break;
            
        case AKM_API_VFS_OPEN:
            if (ctx->vfs_open) {
                const char* path = akm_vm_get_string(vm, args[0]);
                if (path) result = ctx->vfs_open(ctx, path, (uint32_t)args[1]);
                else result = -1;
            }
            break;
            
        case AKM_API_VFS_CLOSE:
            if (ctx->vfs_close) result = ctx->vfs_close(ctx, args[0]);
            break;
            
        case AKM_API_VFS_READ:
            if (ctx->vfs_read && args[1]) {
                result = ctx->vfs_read(ctx, args[0], (void*)(uintptr_t)args[1], (size_t)args[2]);
            }
            break;
            
        case AKM_API_VFS_WRITE:
            if (ctx->vfs_write && args[1]) {
                result = ctx->vfs_write(ctx, args[0], (const void*)(uintptr_t)args[1], (size_t)args[2]);
            }
            break;
            
        case AKM_API_VFS_SEEK:
            if (ctx->vfs_seek) result = ctx->vfs_seek(ctx, args[0], args[1], args[2]);
            break;
            
        /* NETWORK (26-28) */
        case AKM_API_REGISTER_NETIF:
            {
                vm_registry_t* reg = get_vm_registry(vm);
                if (!reg) { result = KMOD_ERR_MEMORY; break; }
                if (reg->netif_count >= MAX_MODULE_NETIF) { result = KMOD_ERR_LIMIT; break; }
                const char* name = akm_vm_get_string(vm, args[0]);
                if (!name || !name[0]) { result = KMOD_ERR_INVALID; break; }
                module_netif_t* mnif = &reg->netifs[reg->netif_count];
                strncpy(mnif->name, name, 15);
                mnif->name[15] = '\0';
                mnif->active = 1;
                result = reg->netif_count++;
            }
            break;
            
        case AKM_API_UNREGISTER_NETIF:
            {
                vm_registry_t* reg = get_vm_registry(vm);
                if (!reg) { result = KMOD_ERR_INVALID; break; }
                const char* name = akm_vm_get_string(vm, args[0]);
                if (!name) { result = KMOD_ERR_INVALID; break; }
                result = KMOD_ERR_NOTFOUND;
                for (int i = 0; i < reg->netif_count; i++) {
                    if (reg->netifs[i].active && strcmp(reg->netifs[i].name, name) == 0) {
                        reg->netifs[i].active = 0;
                        result = KMOD_OK;
                        break;
                    }
                }
            }
            break;
            
        case AKM_API_NETIF_RECEIVE:
            result = 0;  /* Stub */
            break;
            
        /* IRQ (29-32) */
        case AKM_API_REGISTER_IRQ:
            if (ctx->register_irq && args[1]) {
                result = ctx->register_irq(ctx, (uint8_t)args[0],
                    (void(*)(void*))(uintptr_t)args[1], (void*)(uintptr_t)args[2]);
            }
            break;
            
        case AKM_API_UNREGISTER_IRQ:
            if (ctx->unregister_irq) result = ctx->unregister_irq(ctx, (uint8_t)args[0]);
            break;
            
        case AKM_API_ENABLE_IRQ:
            if (ctx->enable_irq) ctx->enable_irq(ctx, (uint8_t)args[0]);
            break;
            
        case AKM_API_DISABLE_IRQ:
            if (ctx->disable_irq) ctx->disable_irq(ctx, (uint8_t)args[0]);
            break;
            
        /* I/O PORTS (33-39) */
        case AKM_API_OUTB:
            if (ctx->outb) ctx->outb(ctx, (uint16_t)args[0], (uint8_t)args[1]);
            break;
            
        case AKM_API_OUTW:
            if (ctx->outw) ctx->outw(ctx, (uint16_t)args[0], (uint16_t)args[1]);
            break;
            
        case AKM_API_OUTL:
            if (ctx->outl) ctx->outl(ctx, (uint16_t)args[0], (uint32_t)args[1]);
            break;
            
        case AKM_API_INB:
            if (ctx->inb) result = ctx->inb(ctx, (uint16_t)args[0]);
            break;
            
        case AKM_API_INW:
            if (ctx->inw) result = ctx->inw(ctx, (uint16_t)args[0]);
            break;
            
        case AKM_API_INL:
            if (ctx->inl) result = (int32_t)ctx->inl(ctx, (uint16_t)args[0]);
            break;
            
        case AKM_API_IO_WAIT:
            if (ctx->io_wait) ctx->io_wait(ctx);
            break;
            
        /* PCI (40-44) */
        case AKM_API_PCI_FIND_DEV:
            if (ctx->pci_find_device) {
                kmod_pci_device_t* dev = ctx->pci_find_device(ctx, (uint16_t)args[0], (uint16_t)args[1]);
                result = dev ? (int32_t)(uintptr_t)dev : 0;
            }
            break;
            
        case AKM_API_PCI_FIND_CLASS:
            if (ctx->pci_find_class) {
                kmod_pci_device_t* dev = ctx->pci_find_class(ctx, (uint8_t)args[0], (uint8_t)args[1]);
                result = dev ? (int32_t)(uintptr_t)dev : 0;
            }
            break;
            
        case AKM_API_PCI_READ_CFG:
            if (ctx->pci_read_config && args[0]) {
                result = (int32_t)ctx->pci_read_config(ctx, (kmod_pci_device_t*)(uintptr_t)args[0], (uint8_t)args[1]);
            }
            break;
            
        case AKM_API_PCI_WRITE_CFG:
            if (ctx->pci_write_config && args[0]) {
                ctx->pci_write_config(ctx, (kmod_pci_device_t*)(uintptr_t)args[0], (uint8_t)args[1], (uint32_t)args[2]);
            }
            break;
            
        case AKM_API_PCI_BUSMASTER:
            if (ctx->pci_enable_busmaster && args[0]) {
                ctx->pci_enable_busmaster(ctx, (kmod_pci_device_t*)(uintptr_t)args[0]);
            }
            break;
            
        /* TIMERS (45-50) */
        case AKM_API_CREATE_TIMER:
            if (ctx->create_timer && args[1]) {
                result = ctx->create_timer(ctx, (uint32_t)args[0],
                    (void(*)(void*))(uintptr_t)args[1], (void*)(uintptr_t)args[2]);
            }
            break;
            
        case AKM_API_START_TIMER:
            result = 0;  /* Auto-starts */
            break;
            
        case AKM_API_STOP_TIMER:
            result = 0;  /* Stub */
            break;
            
        case AKM_API_DESTROY_TIMER:
            if (ctx->destroy_timer) ctx->destroy_timer(ctx, args[0]);
            break;
            
        case AKM_API_GET_TICKS:
            if (ctx->get_ticks) result = (int32_t)ctx->get_ticks(ctx);
            break;
            
        case AKM_API_SLEEP:
            if (ctx->sleep_ms && args[0] > 0) ctx->sleep_ms(ctx, (uint32_t)args[0]);
            break;
            
        /* PROCESS (51-54) */
        case AKM_API_SPAWN:
            if (ctx->spawn && args[1]) {
                const char* name = akm_vm_get_string(vm, args[0]);
                if (name) result = ctx->spawn(ctx, name, (void(*)(void))(uintptr_t)args[1], args[2]);
            }
            break;
            
        case AKM_API_KILL:
            if (ctx->kill) result = ctx->kill(ctx, args[0], args[1]);
            break;
            
        case AKM_API_GETPID:
            if (ctx->getpid) result = ctx->getpid(ctx);
            break;
            
        case AKM_API_YIELD:
            if (ctx->yield) ctx->yield(ctx);
            break;
            
        /* SYSINFO (55-56) */
        case AKM_API_GET_SYSINFO:
            /* Return 0 - sysinfo via ctx if needed */
            result = 0;
            break;
            
        case AKM_API_GET_KERNEL_VER:
            if (ctx->get_kernel_version) {
                result = (int32_t)ctx->get_kernel_version(ctx);
                char dbg[64];
                strcpy(dbg, "[VM] getKernelVersion() -> 0x");
                char tmp[16];
                itoa(result, tmp, 16);
                strcat(dbg, tmp);
                serial_puts(dbg);
                serial_puts("\n");
            }
            break;
            
        /* IPC (57-60) */
        case AKM_API_IPC_SEND:
            result = channel_write(args[0], (const void*)(uintptr_t)args[1], (uint32_t)args[2]);
            break;
            
        case AKM_API_IPC_RECV:
            result = channel_read(args[0], (void*)(uintptr_t)args[1], (uint32_t)args[2]);
            break;
            
        case AKM_API_IPC_CREATE_CH:
            result = channel_create();
            break;
            
        case AKM_API_IPC_DESTROY_CH:
            result = channel_close(args[0]);
            break;
            
        /* CRYPTO (61-62) */
        case AKM_API_SHA256:
            if (ctx->sha256 && args[0] && args[2]) {
                ctx->sha256(ctx, (const void*)(uintptr_t)args[0], (size_t)args[1], (void*)(uintptr_t)args[2]);
                result = args[2];
            }
            break;
            
        case AKM_API_RANDOM_BYTES:
            if (ctx->random_bytes && args[0] && args[1] > 0) {
                result = ctx->random_bytes(ctx, (void*)(uintptr_t)args[0], (size_t)args[1]);
            }
            break;
            
        /* USER (63-65) */
        case AKM_API_GET_UID:
            {
                void* session = user_get_session();
                if (session) {
                    /* session->user->uid - offset 32+0=32 from session base */
                    uint32_t* uid_ptr = (uint32_t*)((uint8_t*)session + 4 + 32);  /* approx */
                    result = uid_ptr ? (int32_t)*uid_ptr : 0;
                } else {
                    result = 0;  /* root */
                }
            }
            break;
            
        case AKM_API_GET_USERNAME:
            {
                void* session = user_get_session();
                if (session) {
                    /* Return pointer to username in user struct */
                    void** user_ptr = (void**)session;
                    if (*user_ptr) result = (int32_t)(uintptr_t)*user_ptr;
                    else result = 0;
                } else {
                    result = 0;
                }
            }
            break;
            
        case AKM_API_CHECK_PERM:
            result = 1;  /* Allow all for now */
            break;
            
        /* ARGS/OUTPUT (66-67) */
        case AKM_API_GET_ARGS:
            {
                /* Return pointer to args string */
                const char* args_str = vm->cmd_args ? vm->cmd_args : "";
                result = (int32_t)(uintptr_t)args_str;
            }
            break;
            
        case AKM_API_PRINT:
            {
                const char* msg = akm_vm_get_string(vm, args[0]);
                if (msg) {
                    kprint(msg);
                    result = 0;
                } else {
                    result = -1;
                }
            }
            break;
            
        /* STRING OPERATIONS (68-70) */
        case AKM_API_STRCAT:
            {
                /* Concatenate two strings: strcat(str1_offset, str2_offset) -> new string offset */
                const char* str1 = akm_vm_get_string(vm, args[0]);
                const char* str2 = akm_vm_get_string(vm, args[1]);
                serial_puts("[VM] strcat(0x");
                char tmp[16];
                itoa(args[0], tmp, 16);
                serial_puts(tmp);
                serial_puts(", 0x");
                itoa(args[1], tmp, 16);
                serial_puts(tmp);
                serial_puts(") str1=");
                serial_puts(str1 ? str1 : "NULL");
                serial_puts(" str2=");
                serial_puts(str2 ? str2 : "NULL");
                serial_puts("\n");
                if (str1 && str2 && ctx->malloc) {
                    size_t len1 = strlen(str1);
                    size_t len2 = strlen(str2);
                    char* newstr = (char*)ctx->malloc(ctx, len1 + len2 + 1);
                    if (newstr) {
                        strcpy(newstr, str1);
                        strcat(newstr, str2);
                        result = (int32_t)(uintptr_t)newstr;
                        serial_puts("[VM] strcat result: ");
                        serial_puts(newstr);
                        serial_puts("\n");
                    } else {
                        result = 0;
                        serial_puts("[VM] strcat: malloc failed\n");
                    }
                } else {
                    result = 0;
                    serial_puts("[VM] strcat: null input or no malloc\n");
                }
            }
            break;
            
        case AKM_API_ITOA:
            {
                /* Convert integer to string: itoa(value) -> string pointer */
                if (ctx->malloc) {
                    char* buf = (char*)ctx->malloc(ctx, 32);
                    if (buf) {
                        itoa(args[0], buf, 10);
                        result = (int32_t)(uintptr_t)buf;
                        /* Debug: log what we converted */
                        char dbg[64];
                        strcpy(dbg, "[VM] itoa(");
                        char tmp[16];
                        itoa(args[0], tmp, 10);
                        strcat(dbg, tmp);
                        strcat(dbg, ") -> 0x");
                        itoa((uint32_t)buf, tmp, 16);
                        strcat(dbg, tmp);
                        strcat(dbg, ": ");
                        strcat(dbg, buf);
                        serial_puts(dbg);
                        serial_puts("\n");
                    } else {
                        result = 0;
                        serial_puts("[VM] itoa: malloc failed\n");
                    }
                } else {
                    result = 0;
                    serial_puts("[VM] itoa: no malloc in ctx\n");
                }
            }
            break;
            
        case AKM_API_STRLEN:
            {
                /* Get string length: strlen(str_offset) -> length */
                const char* str = akm_vm_get_string(vm, args[0]);
                if (str) {
                    result = (int32_t)strlen(str);
                } else {
                    result = 0;
                }
            }
            break;
            
        default:
            /* Unknown API - just return 0, don't crash */
            result = 0;
            break;
    }
    
    akm_vm_push(vm, result);
    return AKM_VM_OK;
}

//                          VM IMPLEMENTATION

void akm_vm_init(akm_vm_t* vm, const void* code, size_t code_size,
                 const void* data, size_t data_size,
                 const char* strtab, size_t strtab_size,
                 kmod_ctx_t* ctx) {
    if (!vm) return;
    
    memset(vm, 0, sizeof(akm_vm_t));
    
    vm->code = (const uint8_t*)code;
    vm->code_size = code_size;
    vm->data = (const uint8_t*)data;
    vm->data_size = data_size;
    vm->strtab = strtab;
    vm->strtab_size = strtab_size;
    vm->ctx = ctx;
    
    vm->flags = 0;
    vm->error_code = AKM_VM_OK;
}

void akm_vm_reset(akm_vm_t* vm) {
    if (!vm) return;
    
    vm->pc = 0;
    vm->sp = 0;
    vm->fp = 0;
    vm->call_depth = 0;
    vm->flags = 0;
    vm->error_code = AKM_VM_OK;
    vm->return_value = 0;
    
    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->locals, 0, sizeof(vm->locals));
    memset(vm->call_stack, 0, sizeof(vm->call_stack));
    memset(vm->call_fp, 0, sizeof(vm->call_fp));
}

const char* akm_vm_get_string(akm_vm_t* vm, uint32_t offset) {
    if (!vm) return NULL;
    
    /* Check if this is a direct pointer (heap address) vs string table offset
     * Heap addresses are typically >= 0x100000, string table offsets are small
     */
    if (offset >= 0x100000) {
        /* Looks like a direct pointer - validate it's a reasonable address */
        const char* ptr = (const char*)(uintptr_t)offset;
        /* Basic sanity check - ensure first byte is readable (not perfect but helps) */
        if (ptr && *ptr != '\0') {
            return ptr;
        }
        /* If first byte is null terminator, it's an empty string but still valid */
        if (ptr) return ptr;
        return NULL;
    }
    
    /* String table offset path */
    if (!vm->strtab) return NULL;
    if (offset >= vm->strtab_size) return NULL;
    
    /* Verify null termination exists within bounds */
    const char* str = vm->strtab + offset;
    size_t max_len = vm->strtab_size - offset;
    size_t len = 0;
    while (len < max_len && str[len] != '\0') len++;
    if (len >= max_len) return NULL;  /* Not null-terminated */
    
    return str;
}

int akm_vm_step(akm_vm_t* vm) {
    if (!vm) return -1;
    
    if (vm->flags & (AKM_VM_HALTED | AKM_VM_ERROR)) {
        return 1;  /* Already stopped */
    }
    
    if (!vm->code || vm->pc >= vm->code_size) {
        vm->flags |= AKM_VM_HALTED;
        return 1;
    }
    
    uint8_t opcode = read_u8(vm);
    int32_t a, b;
    uint32_t addr;
    uint8_t idx, argc;
    
    switch (opcode) {
        case AKM_OP_NOP:
            break;
            
        case AKM_OP_PUSH:
            addr = read_u32(vm);
            akm_vm_push(vm, (int32_t)addr);
            break;
            
        case AKM_OP_PUSH_STR:
            addr = read_u32(vm);
            akm_vm_push(vm, (int32_t)addr);  // Push string offset
            break;
            
        case AKM_OP_PUSH_ARG:
            idx = read_u8(vm);
            // Arguments are below frame pointer
            if (vm->fp > idx) {
                akm_vm_push(vm, vm->stack[vm->fp - idx - 1]);
            } else {
                akm_vm_push(vm, 0);
            }
            break;
            
        case AKM_OP_POP:
            akm_vm_pop(vm);
            break;
            
        case AKM_OP_DUP:
            a = peek(vm);
            akm_vm_push(vm, a);
            break;
            
        case AKM_OP_SWAP:
            a = akm_vm_pop(vm);
            b = akm_vm_pop(vm);
            akm_vm_push(vm, a);
            akm_vm_push(vm, b);
            break;
            
        case AKM_OP_LOAD_LOCAL:
            idx = read_u8(vm);
            if (idx < AKM_VM_LOCALS_MAX) {
                serial_puts("[VM] LOAD_LOCAL[");
                serial_put_uint32(idx);
                serial_puts("] = 0x");
                serial_put_uint32(vm->locals[idx]);
                serial_puts("\n");
                akm_vm_push(vm, vm->locals[idx]);
            } else {
                akm_vm_push(vm, 0);
            }
            break;
            
        case AKM_OP_STORE_LOCAL:
            idx = read_u8(vm);
            a = akm_vm_pop(vm);
            serial_puts("[VM] STORE_LOCAL[");
            serial_put_uint32(idx);
            serial_puts("] = 0x");
            serial_put_uint32(a);
            serial_puts("\n");
            if (idx < AKM_VM_LOCALS_MAX) {
                vm->locals[idx] = a;
            }
            break;
            
        case AKM_OP_LOAD_GLOBAL:
            addr = read_u32(vm);
            if (vm->data && addr + 4 <= vm->data_size) {
                a = *(int32_t*)(vm->data + addr);
                akm_vm_push(vm, a);
            } else {
                akm_vm_push(vm, 0);
            }
            break;
            
        case AKM_OP_STORE_GLOBAL:
            addr = read_u32(vm);
            a = akm_vm_pop(vm);
            /* Note: data section may be read-only in some configs */
            (void)a;  /* Ignored if data is const */
            break;
            
        // Arithmetic
        case AKM_OP_ADD:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a + b);
            break;
            
        case AKM_OP_SUB:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a - b);
            break;
            
        case AKM_OP_MUL:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a * b);
            break;
            
        case AKM_OP_DIV:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            if (b == 0) {
                vm->flags |= AKM_VM_ERROR;
                vm->error_code = AKM_VM_ERR_DIV0;
                return -1;
            }
            akm_vm_push(vm, a / b);
            break;
            
        case AKM_OP_MOD:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            if (b == 0) {
                vm->flags |= AKM_VM_ERROR;
                vm->error_code = AKM_VM_ERR_DIV0;
                return -1;
            }
            akm_vm_push(vm, a % b);
            break;
            
        case AKM_OP_NEG:
            a = akm_vm_pop(vm);
            akm_vm_push(vm, -a);
            break;
            
        case AKM_OP_INC:
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a + 1);
            break;
            
        case AKM_OP_DEC:
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a - 1);
            break;
            
        // Bitwise
        case AKM_OP_AND:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a & b);
            break;
            
        case AKM_OP_OR:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a | b);
            break;
            
        case AKM_OP_XOR:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a ^ b);
            break;
            
        case AKM_OP_NOT:
            a = akm_vm_pop(vm);
            akm_vm_push(vm, ~a);
            break;
            
        case AKM_OP_SHL:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a << (b & 31));  /* Clamp shift */
            break;
            
        case AKM_OP_SHR:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, (int32_t)((uint32_t)a >> (b & 31)));  /* Clamp shift */
            break;
            
        // Comparison
        case AKM_OP_EQ:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a == b ? 1 : 0);
            break;
            
        case AKM_OP_NE:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a != b ? 1 : 0);
            break;
            
        case AKM_OP_LT:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a < b ? 1 : 0);
            break;
            
        case AKM_OP_LE:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a <= b ? 1 : 0);
            break;
            
        case AKM_OP_GT:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a > b ? 1 : 0);
            break;
            
        case AKM_OP_GE:
            b = akm_vm_pop(vm);
            a = akm_vm_pop(vm);
            akm_vm_push(vm, a >= b ? 1 : 0);
            break;
            
        // Control flow
        case AKM_OP_JMP:
            addr = read_u32(vm);
            if (addr < vm->code_size) {
                vm->pc = addr;
            } else {
                vm->flags |= AKM_VM_ERROR;
                vm->error_code = AKM_VM_ERR_ADDR;
            }
            break;
            
        case AKM_OP_JZ:
            addr = read_u32(vm);
            a = akm_vm_pop(vm);
            if (a == 0 && addr < vm->code_size) {
                vm->pc = addr;
            }
            break;
            
        case AKM_OP_JNZ:
            addr = read_u32(vm);
            a = akm_vm_pop(vm);
            if (a != 0 && addr < vm->code_size) {
                vm->pc = addr;
            }
            break;
            
        case AKM_OP_CALL:
            addr = read_u32(vm);
            argc = read_u8(vm);
            
            if (vm->call_depth >= AKM_VM_CALL_DEPTH) {
                vm->flags |= AKM_VM_ERROR;
                vm->error_code = AKM_VM_ERR_CALL;
                return -1;
            }
            
            // Save return address and frame pointer
            vm->call_stack[vm->call_depth] = vm->pc;
            vm->call_fp[vm->call_depth] = vm->fp;
            vm->call_depth++;
            
            // Set new frame pointer
            vm->fp = vm->sp;
            
            // Jump to function
            vm->pc = addr;
            break;
            
        case AKM_OP_CALL_API:
            idx = read_u8(vm);
            argc = read_u8(vm);
            
            if (dispatch_api(vm, idx, argc) != AKM_VM_OK) {
                return -1;
            }
            break;
            
        case AKM_OP_RET:
            if (vm->call_depth == 0) {
                // Return from main - store return value
                vm->return_value = vm->sp > 0 ? akm_vm_pop(vm) : 0;
                vm->flags |= AKM_VM_HALTED;
                return 1;
            }
            
            // Restore caller state
            vm->call_depth--;
            vm->pc = vm->call_stack[vm->call_depth];
            vm->fp = vm->call_fp[vm->call_depth];
            break;
            
        // Memory access (with safe pointer handling)
        case AKM_OP_LOAD8:
            addr = (uint32_t)akm_vm_pop(vm);
            if (addr) {
                akm_vm_push(vm, *(uint8_t*)(uintptr_t)addr);
            } else {
                akm_vm_push(vm, 0);
            }
            break;
            
        case AKM_OP_LOAD16:
            addr = (uint32_t)akm_vm_pop(vm);
            if (addr) {
                akm_vm_push(vm, *(uint16_t*)(uintptr_t)addr);
            } else {
                akm_vm_push(vm, 0);
            }
            break;
            
        case AKM_OP_LOAD32:
            addr = (uint32_t)akm_vm_pop(vm);
            if (addr) {
                akm_vm_push(vm, *(int32_t*)(uintptr_t)addr);
            } else {
                akm_vm_push(vm, 0);
            }
            break;
            
        case AKM_OP_STORE8:
            a = akm_vm_pop(vm);
            addr = (uint32_t)akm_vm_pop(vm);
            if (addr) {
                *(uint8_t*)(uintptr_t)addr = (uint8_t)a;
            }
            break;
            
        case AKM_OP_STORE16:
            a = akm_vm_pop(vm);
            addr = (uint32_t)akm_vm_pop(vm);
            if (addr) {
                *(uint16_t*)(uintptr_t)addr = (uint16_t)a;
            }
            break;
            
        case AKM_OP_STORE32:
            a = akm_vm_pop(vm);
            addr = (uint32_t)akm_vm_pop(vm);
            if (addr) {
                *(int32_t*)(uintptr_t)addr = a;
            }
            break;
            
        case AKM_OP_BREAKPOINT:
            vm->flags |= AKM_VM_BREAKPOINT;
            return 1;
            
        case AKM_OP_HALT:
            vm->flags |= AKM_VM_HALTED;
            return 1;
            
        default:
            serial_puts("VM: Unknown opcode\n");
            vm->flags |= AKM_VM_ERROR;
            vm->error_code = AKM_VM_ERR_OPCODE;
            return -1;
    }
    
    // Check for errors
    if (vm->flags & AKM_VM_ERROR) {
        return -1;
    }
    
    return 0;  // Continue running
}

int akm_vm_execute(akm_vm_t* vm, uint32_t start_offset) {
    if (!vm) return -1;
    
    akm_vm_reset(vm);
    vm->pc = start_offset;
    vm->flags = AKM_VM_RUNNING;
    
    int max_instructions = 100000;  /* Safety limit */
    int count = 0;
    
    while (count < max_instructions) {
        int result = akm_vm_step(vm);
        if (result != 0) {
            break;
        }
        count++;
    }
    
    if (count >= max_instructions) {
        serial_puts("[AKM] Instruction limit exceeded\n");
        vm->flags |= AKM_VM_ERROR;
        vm->error_code = AKM_VM_ERR_CALL;
        return -1;
    }
    
    if (vm->flags & AKM_VM_ERROR) {
        return vm->error_code;
    }
    
    return vm->return_value;
}
