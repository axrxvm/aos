/*
 * === AOS HEADER BEGIN ===
 * include/kmodule_api.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef KMODULE_API_H
#define KMODULE_API_H

/**
 * Kernel Module API v2
 * 
 * This header defines the extended API for kernel modules with capability
 * support and enhanced security features. Modules can use either the basic
 * kmodule.h interface (v1) or this extended API (v2).
 * 
 * v2 modules receive a context pointer with function pointers for all
 * kernel APIs, enabling capability-based security enforcement.
 */

#include <stdint.h>
#include <stddef.h>

// API VERSION

#define KMOD_API_VERSION_MAJOR  2
#define KMOD_API_VERSION_MINOR  0
#define KMOD_API_VERSION_PATCH  0
#define KMOD_API_VERSION        ((KMOD_API_VERSION_MAJOR << 16) | \
                                 (KMOD_API_VERSION_MINOR << 8) | \
                                  KMOD_API_VERSION_PATCH)

// CAPABILITY FLAGS

#define KMOD_CAP_NONE       0x00000000  // No special capabilities
#define KMOD_CAP_COMMAND    0x00000001  // Can register shell commands
#define KMOD_CAP_DRIVER     0x00000002  // Can register device drivers
#define KMOD_CAP_FILESYSTEM 0x00000004  // Can register filesystems
#define KMOD_CAP_NETWORK    0x00000008  // Can access networking
#define KMOD_CAP_ENVVAR     0x00000010  // Can modify environment variables
#define KMOD_CAP_PROCESS    0x00000020  // Can create/manage processes
#define KMOD_CAP_MEMORY     0x00000040  // Can allocate kernel memory
#define KMOD_CAP_IRQ        0x00000080  // Can register IRQ handlers
#define KMOD_CAP_IO_PORT    0x00000100  // Can access I/O ports
#define KMOD_CAP_PCI        0x00000200  // Can access PCI devices
#define KMOD_CAP_TIMER      0x00000400  // Can use kernel timers
#define KMOD_CAP_LOG        0x00000800  // Can write to kernel log
#define KMOD_CAP_SYSINFO    0x00001000  // Can read system information
#define KMOD_CAP_USER       0x00002000  // Can manage users
#define KMOD_CAP_SECURITY   0x00004000  // Can modify security settings
#define KMOD_CAP_PANIC      0x00008000  // Can trigger kernel panic
#define KMOD_CAP_DEBUG      0x00010000  // Can access debug features
#define KMOD_CAP_IPC        0x00020000  // Can use IPC mechanisms
#define KMOD_CAP_CRYPTO     0x00040000  // Can use crypto functions
#define KMOD_CAP_ACPI       0x00080000  // Can access ACPI
#define KMOD_CAP_ALL        0xFFFFFFFF  // All capabilities

// Common capability combinations
#define KMOD_CAP_BASIC      (KMOD_CAP_LOG | KMOD_CAP_MEMORY | KMOD_CAP_SYSINFO)
#define KMOD_CAP_SHELL      (KMOD_CAP_BASIC | KMOD_CAP_COMMAND | KMOD_CAP_ENVVAR)
#define KMOD_CAP_DEVICE     (KMOD_CAP_BASIC | KMOD_CAP_DRIVER | KMOD_CAP_IRQ | \
                             KMOD_CAP_IO_PORT | KMOD_CAP_PCI)
#define KMOD_CAP_NETDEV     (KMOD_CAP_DEVICE | KMOD_CAP_NETWORK)

// ERROR CODES

#define KMOD_OK              0
#define KMOD_ERR_INVALID    -1   // Invalid argument
#define KMOD_ERR_MEMORY     -2   // Memory allocation failed
#define KMOD_ERR_NOTFOUND   -3   // Module/resource not found
#define KMOD_ERR_LOADED     -4   // Module already loaded
#define KMOD_ERR_VERSION    -5   // Version mismatch
#define KMOD_ERR_CAPABILITY -6   // Insufficient capabilities
#define KMOD_ERR_IO         -7   // I/O error
#define KMOD_ERR_INIT       -8   // Initialization failed
#define KMOD_ERR_API        -9   // API version mismatch
#define KMOD_ERR_DEPENDENCY -10  // Missing dependency
#define KMOD_ERR_SECURITY   -11  // Security violation
#define KMOD_ERR_LIMIT      -12  // Resource limit reached

// LOG LEVELS

#define KMOD_LOG_EMERG      0   // System is unusable
#define KMOD_LOG_ALERT      1   // Action must be taken immediately
#define KMOD_LOG_CRIT       2   // Critical conditions
#define KMOD_LOG_ERR        3   // Error conditions
#define KMOD_LOG_WARNING    4   // Warning conditions
#define KMOD_LOG_NOTICE     5   // Normal but significant condition
#define KMOD_LOG_INFO       6   // Informational
#define KMOD_LOG_DEBUG      7   // Debug-level messages

// DRIVER TYPES

#define KMOD_DRV_CHAR       1   // Character device
#define KMOD_DRV_BLOCK      2   // Block device
#define KMOD_DRV_NET        3   // Network device
#define KMOD_DRV_INPUT      4   // Input device
#define KMOD_DRV_DISPLAY    5   // Display device
#define KMOD_DRV_SOUND      6   // Sound device
#define KMOD_DRV_STORAGE    7   // Storage controller
#define KMOD_DRV_BUS        8   // Bus controller

// PCI DEVICE INFO

typedef struct kmod_pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  irq;
    uint32_t bar[6];
} kmod_pci_device_t;

// SYSTEM INFO

typedef struct kmod_sysinfo {
    uint32_t kernel_version;
    uint32_t api_version;
    uint32_t total_memory;
    uint32_t free_memory;
    uint32_t uptime_ticks;
    uint32_t cpu_count;
    uint32_t module_count;
    char     kernel_name[32];
    char     arch[16];
} kmod_sysinfo_t;

// COMMAND DESCRIPTOR

typedef struct kmod_command {
    const char* name;
    const char* syntax;
    const char* description;
    const char* category;
    int (*handler)(int argc, char** argv);
} kmod_command_t;

// MODULE CONTEXT

typedef struct kmod_ctx kmod_ctx_t;

/**
 * Module Context Structure
 * 
 * This is the primary interface between modules and the kernel.
 * All kernel API calls go through function pointers in this structure,
 * allowing the kernel to enforce capability checks.
 */
struct kmod_ctx {
    // Module Identification
    const char* name;               // Module name
    uint32_t    capabilities;       // Granted capabilities
    uint32_t    api_version;        // API version supported
    
    // Logging (KMOD_CAP_LOG)
    void (*log)(kmod_ctx_t* ctx, int level, const char* fmt, ...);
    void (*log_hex)(kmod_ctx_t* ctx, const void* data, size_t len);
    
    // Memory Management (KMOD_CAP_MEMORY)
    void* (*malloc)(kmod_ctx_t* ctx, size_t size);
    void* (*calloc)(kmod_ctx_t* ctx, size_t nmemb, size_t size);
    void* (*realloc)(kmod_ctx_t* ctx, void* ptr, size_t size);
    void  (*free)(kmod_ctx_t* ctx, void* ptr);
    void* (*alloc_page)(kmod_ctx_t* ctx);
    void  (*free_page)(kmod_ctx_t* ctx, void* page);
    
    // Command Registration (KMOD_CAP_COMMAND)
    int (*register_command)(kmod_ctx_t* ctx, const kmod_command_t* cmd);
    int (*unregister_command)(kmod_ctx_t* ctx, const char* name);
    
    // Environment Variables (KMOD_CAP_ENVVAR)
    const char* (*getenv)(kmod_ctx_t* ctx, const char* name);
    int (*setenv)(kmod_ctx_t* ctx, const char* name, const char* value);
    int (*unsetenv)(kmod_ctx_t* ctx, const char* name);
    
    // I/O Ports (KMOD_CAP_IO_PORT)
    void     (*outb)(kmod_ctx_t* ctx, uint16_t port, uint8_t value);
    void     (*outw)(kmod_ctx_t* ctx, uint16_t port, uint16_t value);
    void     (*outl)(kmod_ctx_t* ctx, uint16_t port, uint32_t value);
    uint8_t  (*inb)(kmod_ctx_t* ctx, uint16_t port);
    uint16_t (*inw)(kmod_ctx_t* ctx, uint16_t port);
    uint32_t (*inl)(kmod_ctx_t* ctx, uint16_t port);
    void     (*io_wait)(kmod_ctx_t* ctx);
    
    // PCI Access (KMOD_CAP_PCI)
    kmod_pci_device_t* (*pci_find_device)(kmod_ctx_t* ctx, uint16_t vendor, uint16_t device);
    kmod_pci_device_t* (*pci_find_class)(kmod_ctx_t* ctx, uint8_t class_code, uint8_t subclass);
    uint32_t (*pci_read_config)(kmod_ctx_t* ctx, kmod_pci_device_t* dev, uint8_t offset);
    void (*pci_write_config)(kmod_ctx_t* ctx, kmod_pci_device_t* dev, uint8_t offset, uint32_t val);
    void (*pci_enable_busmaster)(kmod_ctx_t* ctx, kmod_pci_device_t* dev);
    
    // Timer Functions (KMOD_CAP_TIMER)
    uint32_t (*get_ticks)(kmod_ctx_t* ctx);
    void (*sleep_ms)(kmod_ctx_t* ctx, uint32_t ms);
    int (*create_timer)(kmod_ctx_t* ctx, uint32_t interval_ms, 
                       void (*callback)(void* data), void* data);
    int (*start_timer)(kmod_ctx_t* ctx, int timer_id);
    int (*stop_timer)(kmod_ctx_t* ctx, int timer_id);
    void (*destroy_timer)(kmod_ctx_t* ctx, int timer_id);
    
    // System Info (KMOD_CAP_SYSINFO)
    int (*get_sysinfo)(kmod_ctx_t* ctx, kmod_sysinfo_t* info);
    uint32_t (*get_kernel_version)(kmod_ctx_t* ctx);
    
    // File Operations (KMOD_CAP_FILESYSTEM)
    int (*vfs_open)(kmod_ctx_t* ctx, const char* path, uint32_t flags);
    int (*vfs_close)(kmod_ctx_t* ctx, int fd);
    int (*vfs_read)(kmod_ctx_t* ctx, int fd, void* buf, size_t size);
    int (*vfs_write)(kmod_ctx_t* ctx, int fd, const void* buf, size_t size);
    int (*vfs_seek)(kmod_ctx_t* ctx, int fd, int32_t offset, int whence);
    
    // IRQ Management (KMOD_CAP_IRQ)
    int (*register_irq)(kmod_ctx_t* ctx, uint8_t irq, 
                       void (*handler)(void* data), void* data);
    int (*unregister_irq)(kmod_ctx_t* ctx, uint8_t irq);
    void (*enable_irq)(kmod_ctx_t* ctx, uint8_t irq);
    void (*disable_irq)(kmod_ctx_t* ctx, uint8_t irq);
    
    // Process Management (KMOD_CAP_PROCESS)
    int (*spawn)(kmod_ctx_t* ctx, const char* name, void (*entry)(void), int priority);
    int (*kill)(kmod_ctx_t* ctx, int pid, int signal);
    int (*getpid)(kmod_ctx_t* ctx);
    void (*yield)(kmod_ctx_t* ctx);
    
    // Crypto Functions (KMOD_CAP_CRYPTO)
    void (*sha256)(kmod_ctx_t* ctx, const void* data, size_t len, uint8_t* hash);
    int (*random_bytes)(kmod_ctx_t* ctx, void* buf, size_t len);
    
    // Private Data (kernel internal)
    void* _private;
    void* _module;
};

//  MODULE ENTRY POINTS

// v2 module function signatures
typedef int  (*kmod_init_fn)(kmod_ctx_t* ctx);
typedef void (*kmod_exit_fn)(kmod_ctx_t* ctx);

// V2 FILE FORMAT

#define AKM_MAGIC_V2        0x324D4B41  // "AKM2"
#define AKM_FORMAT_V2       2

// v2 module header (512 bytes total, page-aligned sections)
typedef struct {
    // Basic identification (64 bytes)
    uint32_t magic;                     // 0x324D4B41 "AKM2"
    uint16_t format_version;            // Format version (2)
    uint16_t flags;                     // Module flags
    uint32_t header_size;               // Size of this header (512)
    uint32_t total_size;                // Total file size
    
    // Module info (96 bytes)
    char     name[32];                  // Module name
    char     version[16];               // Module version string
    char     author[32];                // Author name
    uint16_t api_version;               // Required API version
    uint16_t reserved1;
    
    // Kernel compatibility (16 bytes)
    uint32_t kernel_min_version;        // Minimum kernel version
    uint32_t kernel_max_version;        // Maximum kernel version (0 = any)
    uint32_t capabilities;              // Required capabilities
    uint32_t reserved2;
    
    // Section info (48 bytes)
    uint32_t code_offset;               // Offset to code section
    uint32_t code_size;                 // Code section size
    uint32_t data_offset;               // Offset to data section
    uint32_t data_size;                 // Data section size
    uint32_t rodata_offset;             // Offset to read-only data
    uint32_t rodata_size;               // Read-only data size
    uint32_t bss_size;                  // BSS section size
    uint32_t reserved3[5];
    
    // Entry points (16 bytes)
    uint32_t init_offset;               // Offset to init function
    uint32_t cleanup_offset;            // Offset to cleanup function
    uint32_t reserved4[2];
    
    // Symbol/string tables (32 bytes)
    uint32_t symtab_offset;             // Symbol table offset
    uint32_t symtab_size;               // Symbol table size
    uint32_t strtab_offset;             // String table offset
    uint32_t strtab_size;               // String table size
    uint32_t reserved5[4];
    
    // Dependencies (136 bytes)
    uint8_t  dep_count;                 // Number of dependencies
    uint8_t  reserved6[3];
    char     dependencies[4][32];       // Dependency names
    
    // Security (104 bytes)
    uint8_t  security_level;            // 0=none, 1=basic, 2=strict
    uint8_t  signature_type;            // 0=none, 1=sha256
    uint8_t  reserved7[2];
    uint32_t header_checksum;           // CRC32 of header (excluding this field)
    uint32_t content_checksum;          // CRC32 of all sections
    uint8_t  signature[64];             // Digital signature
    uint8_t  reserved8[28];
    
    // Padding to reach 512 bytes (64 bytes)
    uint8_t  _padding[64];
} __attribute__((packed)) akm_header_v2_t;

// Header flags
#define AKM_FLAG_DEBUG      0x0001      // Debug build
#define AKM_FLAG_NATIVE     0x0002      // Native code (not bytecode)
#define AKM_FLAG_REQUIRED   0x0004      // Required for system operation
#define AKM_FLAG_AUTOLOAD   0x0008      // Auto-load at boot

// Verify header size at compile time
_Static_assert(sizeof(akm_header_v2_t) == 512, "akm_header_v2_t must be 512 bytes");

#endif // KMODULE_API_H
