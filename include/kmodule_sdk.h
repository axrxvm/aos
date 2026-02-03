/*
 * === AOS HEADER BEGIN ===
 * ./include/kmodule_sdk.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.7
 * === AOS HEADER END ===
 */

#ifndef KMODULE_SDK_H
#define KMODULE_SDK_H

/**
 * Kernel Module SDK
 * 
 * Developer-friendly macros and helpers for writing kernel modules.
 * Include this header in your module for convenient API access.
 */

#include <kmodule_api.h>

// MODULE DECLARATION

/**
 * Declare a kernel module
 * 
 * Usage:
 *   AKM_MODULE("my-module", "1.0.0", "Author Name", 
 *              "Module description", KMOD_CAP_LOG | KMOD_CAP_COMMAND);
 */
#define AKM_MODULE(name, version, author, description, caps) \
    static const char* __akm_name = name; \
    static const char* __akm_version = version; \
    static const char* __akm_author = author; \
    static const char* __akm_description = description; \
    static const uint32_t __akm_capabilities = (caps); \
    \
    /* Suppress unused variable warnings */ \
    __attribute__((used)) static const void* __akm_info[] = { \
        __akm_name, __akm_version, __akm_author, __akm_description, \
        (void*)(uintptr_t)__akm_capabilities \
    }

//                          CONTEXT ACCESS

// Global context pointer (set by mod_init)
static kmod_ctx_t* __akm_ctx = ((void*)0);

/**
 * Set the module context (call this first in mod_init)
 */
#define akm_set_ctx(ctx) do { __akm_ctx = (ctx); } while(0)

/**
 * Get the module context
 */
#define akm_get_ctx() (__akm_ctx)

//                           LOGGING MACROS

#define akm_log(level, fmt, ...) \
    do { if (__akm_ctx && __akm_ctx->log) \
        __akm_ctx->log(__akm_ctx, (level), fmt, ##__VA_ARGS__); \
    } while(0)

#define akm_emerg(fmt, ...)   akm_log(KMOD_LOG_EMERG, fmt, ##__VA_ARGS__)
#define akm_alert(fmt, ...)   akm_log(KMOD_LOG_ALERT, fmt, ##__VA_ARGS__)
#define akm_crit(fmt, ...)    akm_log(KMOD_LOG_CRIT, fmt, ##__VA_ARGS__)
#define akm_err(fmt, ...)     akm_log(KMOD_LOG_ERR, fmt, ##__VA_ARGS__)
#define akm_warn(fmt, ...)    akm_log(KMOD_LOG_WARNING, fmt, ##__VA_ARGS__)
#define akm_notice(fmt, ...)  akm_log(KMOD_LOG_NOTICE, fmt, ##__VA_ARGS__)
#define akm_info(fmt, ...)    akm_log(KMOD_LOG_INFO, fmt, ##__VA_ARGS__)
#define akm_debug(fmt, ...)   akm_log(KMOD_LOG_DEBUG, fmt, ##__VA_ARGS__)

// Simple string logging (no format)
#define akm_puts(str) akm_info("%s", str)

//                         MEMORY MACROS

#define akm_malloc(size) \
    (__akm_ctx && __akm_ctx->malloc ? __akm_ctx->malloc(__akm_ctx, size) : ((void*)0))

#define akm_calloc(nmemb, size) \
    (__akm_ctx && __akm_ctx->calloc ? __akm_ctx->calloc(__akm_ctx, nmemb, size) : ((void*)0))

#define akm_realloc(ptr, size) \
    (__akm_ctx && __akm_ctx->realloc ? __akm_ctx->realloc(__akm_ctx, ptr, size) : ((void*)0))

#define akm_free(ptr) \
    do { if (__akm_ctx && __akm_ctx->free) __akm_ctx->free(__akm_ctx, ptr); } while(0)

#define akm_alloc_page() \
    (__akm_ctx && __akm_ctx->alloc_page ? __akm_ctx->alloc_page(__akm_ctx) : ((void*)0))

#define akm_free_page(page) \
    do { if (__akm_ctx && __akm_ctx->free_page) __akm_ctx->free_page(__akm_ctx, page); } while(0)

//                        COMMAND REGISTRATION

/**
 * Register a shell command
 * 
 * Usage:
 *   AKM_COMMAND("mycmd", "mycmd [args]", "Description", "category", handler_fn);
 */
#define AKM_COMMAND(name, syntax, desc, category, handler) \
    do { \
        if (__akm_ctx && __akm_ctx->register_command) { \
            kmod_command_t cmd = { name, syntax, desc, category, handler }; \
            __akm_ctx->register_command(__akm_ctx, &cmd); \
        } \
    } while(0)

#define akm_unregister_command(name) \
    do { if (__akm_ctx && __akm_ctx->unregister_command) \
        __akm_ctx->unregister_command(__akm_ctx, name); \
    } while(0)

//                      ENVIRONMENT VARIABLES

#define akm_getenv(name) \
    (__akm_ctx && __akm_ctx->getenv ? __akm_ctx->getenv(__akm_ctx, name) : ((void*)0))

#define akm_setenv(name, value) \
    (__akm_ctx && __akm_ctx->setenv ? __akm_ctx->setenv(__akm_ctx, name, value) : -1)

#define akm_unsetenv(name) \
    (__akm_ctx && __akm_ctx->unsetenv ? __akm_ctx->unsetenv(__akm_ctx, name) : -1)

//                          I/O PORT ACCESS

#define akm_outb(port, val) \
    do { if (__akm_ctx && __akm_ctx->outb) __akm_ctx->outb(__akm_ctx, port, val); } while(0)

#define akm_outw(port, val) \
    do { if (__akm_ctx && __akm_ctx->outw) __akm_ctx->outw(__akm_ctx, port, val); } while(0)

#define akm_outl(port, val) \
    do { if (__akm_ctx && __akm_ctx->outl) __akm_ctx->outl(__akm_ctx, port, val); } while(0)

#define akm_inb(port) \
    (__akm_ctx && __akm_ctx->inb ? __akm_ctx->inb(__akm_ctx, port) : 0)

#define akm_inw(port) \
    (__akm_ctx && __akm_ctx->inw ? __akm_ctx->inw(__akm_ctx, port) : 0)

#define akm_inl(port) \
    (__akm_ctx && __akm_ctx->inl ? __akm_ctx->inl(__akm_ctx, port) : 0)

#define akm_io_wait() \
    do { if (__akm_ctx && __akm_ctx->io_wait) __akm_ctx->io_wait(__akm_ctx); } while(0)

//                           PCI ACCESS

#define akm_pci_find_device(vendor, device) \
    (__akm_ctx && __akm_ctx->pci_find_device ? \
        __akm_ctx->pci_find_device(__akm_ctx, vendor, device) : ((void*)0))

#define akm_pci_find_class(class_code, subclass) \
    (__akm_ctx && __akm_ctx->pci_find_class ? \
        __akm_ctx->pci_find_class(__akm_ctx, class_code, subclass) : ((void*)0))

#define akm_pci_read_config(dev, offset) \
    (__akm_ctx && __akm_ctx->pci_read_config ? \
        __akm_ctx->pci_read_config(__akm_ctx, dev, offset) : 0)

#define akm_pci_write_config(dev, offset, val) \
    do { if (__akm_ctx && __akm_ctx->pci_write_config) \
        __akm_ctx->pci_write_config(__akm_ctx, dev, offset, val); \
    } while(0)

#define akm_pci_enable_busmaster(dev) \
    do { if (__akm_ctx && __akm_ctx->pci_enable_busmaster) \
        __akm_ctx->pci_enable_busmaster(__akm_ctx, dev); \
    } while(0)

//                            TIMERS

#define akm_get_ticks() \
    (__akm_ctx && __akm_ctx->get_ticks ? __akm_ctx->get_ticks(__akm_ctx) : 0)

#define akm_sleep(ms) \
    do { if (__akm_ctx && __akm_ctx->sleep_ms) __akm_ctx->sleep_ms(__akm_ctx, ms); } while(0)

#define akm_create_timer(interval_ms, callback, data) \
    (__akm_ctx && __akm_ctx->create_timer ? \
        __akm_ctx->create_timer(__akm_ctx, interval_ms, callback, data) : -1)

#define akm_destroy_timer(timer_id) \
    do { if (__akm_ctx && __akm_ctx->destroy_timer) \
        __akm_ctx->destroy_timer(__akm_ctx, timer_id); \
    } while(0)

//                          SYSTEM INFO

#define akm_get_sysinfo(info) \
    (__akm_ctx && __akm_ctx->get_sysinfo ? \
        __akm_ctx->get_sysinfo(__akm_ctx, info) : -1)

#define akm_get_kernel_version() \
    (__akm_ctx && __akm_ctx->get_kernel_version ? \
        __akm_ctx->get_kernel_version(__akm_ctx) : 0)

//                        FILE OPERATIONS

#define akm_open(path, flags) \
    (__akm_ctx && __akm_ctx->vfs_open ? __akm_ctx->vfs_open(__akm_ctx, path, flags) : -1)

#define akm_close(fd) \
    (__akm_ctx && __akm_ctx->vfs_close ? __akm_ctx->vfs_close(__akm_ctx, fd) : -1)

#define akm_read(fd, buf, size) \
    (__akm_ctx && __akm_ctx->vfs_read ? __akm_ctx->vfs_read(__akm_ctx, fd, buf, size) : -1)

#define akm_write(fd, buf, size) \
    (__akm_ctx && __akm_ctx->vfs_write ? __akm_ctx->vfs_write(__akm_ctx, fd, buf, size) : -1)

#define akm_seek(fd, offset, whence) \
    (__akm_ctx && __akm_ctx->vfs_seek ? __akm_ctx->vfs_seek(__akm_ctx, fd, offset, whence) : -1)

//                        IRQ MANAGEMENT

#define akm_register_irq(irq, handler, data) \
    (__akm_ctx && __akm_ctx->register_irq ? \
        __akm_ctx->register_irq(__akm_ctx, irq, handler, data) : -1)

#define akm_unregister_irq(irq) \
    (__akm_ctx && __akm_ctx->unregister_irq ? \
        __akm_ctx->unregister_irq(__akm_ctx, irq) : -1)

#define akm_enable_irq(irq) \
    do { if (__akm_ctx && __akm_ctx->enable_irq) __akm_ctx->enable_irq(__akm_ctx, irq); } while(0)

#define akm_disable_irq(irq) \
    do { if (__akm_ctx && __akm_ctx->disable_irq) __akm_ctx->disable_irq(__akm_ctx, irq); } while(0)

//                       PROCESS MANAGEMENT

#define akm_spawn(name, entry, priority) \
    (__akm_ctx && __akm_ctx->spawn ? __akm_ctx->spawn(__akm_ctx, name, entry, priority) : -1)

#define akm_kill(pid, signal) \
    (__akm_ctx && __akm_ctx->kill ? __akm_ctx->kill(__akm_ctx, pid, signal) : -1)

#define akm_getpid() \
    (__akm_ctx && __akm_ctx->getpid ? __akm_ctx->getpid(__akm_ctx) : -1)

#define akm_yield() \
    do { if (__akm_ctx && __akm_ctx->yield) __akm_ctx->yield(__akm_ctx); } while(0)

//                          CRYPTO

#define akm_sha256(data, len, hash) \
    do { if (__akm_ctx && __akm_ctx->sha256) __akm_ctx->sha256(__akm_ctx, data, len, hash); } while(0)

#define akm_random_bytes(buf, len) \
    (__akm_ctx && __akm_ctx->random_bytes ? __akm_ctx->random_bytes(__akm_ctx, buf, len) : -1)

//                       UTILITY MACROS

#define AKM_ARRAY_SIZE(arr)     (sizeof(arr) / sizeof((arr)[0]))
#define AKM_MIN(a, b)           ((a) < (b) ? (a) : (b))
#define AKM_MAX(a, b)           ((a) > (b) ? (a) : (b))
#define AKM_CLAMP(x, lo, hi)    AKM_MIN(AKM_MAX(x, lo), hi)
#define AKM_BIT(n)              (1U << (n))
#define AKM_ALIGN(x, a)         (((x) + (a) - 1) & ~((a) - 1))

// Check if capability is granted
#define AKM_HAS_CAP(cap) \
    (__akm_ctx && (__akm_ctx->capabilities & (cap)))

// Capability check with early return
#define AKM_REQUIRE_CAP(cap) \
    do { if (!AKM_HAS_CAP(cap)) { \
        akm_err("Missing capability: 0x%x", cap); \
        return KMOD_ERR_CAPABILITY; \
    } } while(0)

// Version helpers
#define AKM_VERSION(maj, min, pat) \
    (((maj) << 16) | ((min) << 8) | (pat))

#define AKM_VERSION_MAJOR(v)    (((v) >> 16) & 0xFF)
#define AKM_VERSION_MINOR(v)    (((v) >> 8) & 0xFF)
#define AKM_VERSION_PATCH(v)    ((v) & 0xFF)

#endif // KMODULE_SDK_H
