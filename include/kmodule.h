/*
 * === AOS HEADER BEGIN ===
 * include/kmodule.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef KMODULE_H
#define KMODULE_H

#include <stdint.h>
#include <stddef.h>
#include <stddef.h>

// aOS Kernel Module system (.akm files, not .ko)
#define MAX_MODULES 32
#define MODULE_NAME_LEN 32
#define MODULE_VERSION_LEN 16

// Module states
typedef enum {
    MODULE_UNLOADED,
    MODULE_LOADING,
    MODULE_LOADED,
    MODULE_UNLOADING,
    MODULE_ERROR
} module_state_t;

// Module function pointers
typedef int (*module_init_fn)(void);
typedef void (*module_cleanup_fn)(void);

// Module structure
typedef struct kmodule {
    char name[MODULE_NAME_LEN];
    char version[MODULE_VERSION_LEN];
    module_state_t state;
    
    // Module code and data
    void* code_base;
    uint32_t code_size;
    void* data_base;
    uint32_t data_size;
    
    // Entry points
    module_init_fn init;
    module_cleanup_fn cleanup;
    
    // Dependencies
    char dependencies[4][MODULE_NAME_LEN];
    int dep_count;
    
    // Metadata
    uint32_t load_time;
    uint32_t ref_count;
    uint32_t task_id;   // Process system TID for this module
    
    struct kmodule* next;
} kmodule_t;

// Module header in .akm file
typedef struct {
    uint32_t magic;          // "AKM\0" = 0x004D4B41
    uint32_t version;        // Module format version
    uint32_t kernel_version; // Required kernel version
    char name[MODULE_NAME_LEN];
    char mod_version[MODULE_VERSION_LEN];
    uint32_t code_size;
    uint32_t data_size;
    uint32_t init_offset;    // Offset to init function
    uint32_t cleanup_offset; // Offset to cleanup function
    uint32_t checksum;
} akm_header_t;

// Initialize module system
void init_kmodules(void);

// Module operations
int kmodule_load(const char* path);
int kmodule_unload(const char* name);
kmodule_t* kmodule_find(const char* name);
void kmodule_list(void);

// Version compatibility check
int kmodule_check_version(uint32_t module_version);

// Get kernel version
uint32_t kernel_get_version(void);

//                          V2 MODULE SYSTEM

// Forward declare context type (full definition in kmodule_api.h)
struct kmod_ctx;
typedef struct kmod_ctx kmod_ctx_t;

// V2 entry (for internal use)
typedef struct kmod_v2_entry kmod_v2_entry_t;

// Initialize v2 module system
void init_kmodules_v2(void);

// Check if module data is v2 format
int kmodule_is_v2(const void* data, size_t len);

// Load/unload v2 modules
int kmodule_load_v2(const void* data, size_t len);
int kmodule_unload_v2(const char* name);

// List v2 modules
void kmodule_list_v2(void);

// Drive module-managed timer callbacks (called from scheduler tick)
void kmodule_v2_timer_tick(void);

// Get module context by name
kmod_ctx_t* kmodule_get_context(const char* name);

// Get v2 module count
int kmodule_count_v2(void);

#endif // KMODULE_H
