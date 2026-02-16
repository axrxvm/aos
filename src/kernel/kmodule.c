/*
 * === AOS HEADER BEGIN ===
 * src/kernel/kmodule.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <kmodule.h>
#include <string.h>
#include <serial.h>
#include <syscall.h>
#include <vmm.h>
#include <version.h>
#include <memory.h>
#include <process.h>
#include <stdlib.h>

static kmodule_t* module_list = NULL;
static int module_count = 0;

// Current kernel version (major.minor.patch encoded)
#define KERNEL_VERSION_ENCODE(maj, min, pat) (((maj) << 16) | ((min) << 8) | (pat))

void init_kmodules(void) {
    serial_puts("Initializing kernel module system...\n");
    module_list = NULL;
    module_count = 0;
    serial_puts("Kernel module system initialized (.akm support enabled)\n");
    
    // Initialize v2 module system as well
    init_kmodules_v2();
}

uint32_t kernel_get_version(void) {
    return KERNEL_VERSION_ENCODE(AOS_VERSION_MAJOR, AOS_VERSION_MINOR, AOS_VERSION_PATCH);
}

int kmodule_check_version(uint32_t module_version) {
    uint32_t kernel_ver = kernel_get_version();
    
    // Extract major versions
    uint32_t kernel_major = (kernel_ver >> 16) & 0xFF;
    uint32_t module_major = (module_version >> 16) & 0xFF;
    
    // Major version must match
    if (kernel_major != module_major) {
        serial_puts("Module version mismatch: major version incompatible\n");
        return -1;
    }
    
    return 0;
}

kmodule_t* kmodule_find(const char* name) {
    if (!name) return NULL;
    
    kmodule_t* current = module_list;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

int kmodule_load(const char* path) {
    if (!path) {
        serial_puts("Error: NULL path provided to kmodule_load\n");
        return -1;
    }
    
    serial_puts("Loading kernel module: ");
    serial_puts(path);
    serial_puts("\n");
    
    // Open module file
    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        serial_puts("Error: Failed to open module file '");
        serial_puts(path);
        serial_puts("'\n");
        return -1;
    }
    
    // Read header
    akm_header_t header;
    if (sys_read(fd, &header, sizeof(akm_header_t)) != sizeof(akm_header_t)) {
        sys_close(fd);
        serial_puts("Error: Failed to read module header\n");
        return -1;
    }
    
    // Validate magic
    if (header.magic != 0x004D4B41) {  // "AKM\0"
        sys_close(fd);
        serial_puts("Error: Invalid module format (bad magic)\n");
        return -1;
    }
    
    // Check version compatibility
    if (kmodule_check_version(header.kernel_version) != 0) {
        sys_close(fd);
        serial_puts("Error: Module kernel version incompatible\n");
        return -1;
    }
    
    // Check if already loaded
    if (kmodule_find(header.name)) {
        sys_close(fd);
        serial_puts("Error: Module already loaded\n");
        return -1;
    }
    
    // Allocate module structure
    kmodule_t* module = (kmodule_t*)kmalloc(sizeof(kmodule_t));
    if (!module) {
        sys_close(fd);
        serial_puts("Error: Failed to allocate module structure\n");
        return -1;
    }
    
    memset(module, 0, sizeof(kmodule_t));
    strncpy(module->name, header.name, MODULE_NAME_LEN - 1);
    strncpy(module->version, header.mod_version, MODULE_VERSION_LEN - 1);
    module->state = MODULE_LOADING;
    module->code_size = header.code_size;
    module->data_size = header.data_size;
    
    // Allocate code space
    module->code_base = kmalloc(header.code_size);
    if (!module->code_base) {
        kfree(module);
        sys_close(fd);
        serial_puts("Error: Failed to allocate code space\n");
        return -1;
    }
    
    // Read code
    if (sys_read(fd, module->code_base, header.code_size) != (int)header.code_size) {
        kfree(module->code_base);
        kfree(module);
        sys_close(fd);
        serial_puts("Error: Failed to read module code\n");
        return -1;
    }
    
    // Allocate data space if needed
    if (header.data_size > 0) {
        module->data_base = kmalloc(header.data_size);
        if (!module->data_base) {
            kfree(module->code_base);
            kfree(module);
            sys_close(fd);
            serial_puts("Error: Failed to allocate data space\n");
            return -1;
        }
        
        // Read data
        if (sys_read(fd, module->data_base, header.data_size) != (int)header.data_size) {
            kfree(module->data_base);
            kfree(module->code_base);
            kfree(module);
            sys_close(fd);
            serial_puts("Error: Failed to read module data\n");
            return -1;
        }
    }
    
    sys_close(fd);
    
    // Set up function pointers
    module->init = (module_init_fn)((char*)module->code_base + header.init_offset);
    module->cleanup = (module_cleanup_fn)((char*)module->code_base + header.cleanup_offset);
    
    // Call init function
    if (module->init) {
        int result = module->init();
        if (result != 0) {
            serial_puts("Error: Module initialization failed\n");
            if (module->data_base) kfree(module->data_base);
            kfree(module->code_base);
            kfree(module);
            return -1;
        }
    }
    
    // Add to list
    char task_name[MODULE_NAME_LEN + 6];
    snprintf(task_name, sizeof(task_name), "kmod:%s", module->name);
    pid_t task_id = process_register_kernel_task(task_name, TASK_TYPE_MODULE, PRIORITY_HIGH);
    if (task_id > 0) {
        module->task_id = (uint32_t)task_id;
    }

    module->state = MODULE_LOADED;
    module->next = module_list;
    module_list = module;
    module_count++;
    
    serial_puts("Module loaded successfully: ");
    serial_puts(module->name);
    serial_puts(" v");
    serial_puts(module->version);
    serial_puts("\n");
    
    return 0;
}

int kmodule_unload(const char* name) {
    if (!name) return -1;
    
    kmodule_t* module = kmodule_find(name);
    if (!module) {
        serial_puts("Error: Module not found\n");
        return -1;
    }
    
    if (module->ref_count > 0) {
        serial_puts("Error: Module in use (ref_count > 0)\n");
        return -1;
    }
    
    module->state = MODULE_UNLOADING;
    
    // Call cleanup function
    if (module->cleanup) {
        module->cleanup();
    }
    
    // Remove from list
    kmodule_t** prev = &module_list;
    kmodule_t* current = module_list;
    while (current) {
        if (current == module) {
            *prev = current->next;
            break;
        }
        prev = &current->next;
        current = current->next;
    }
    
    // Free memory
    if (module->task_id != 0) {
        process_finish_kernel_task((pid_t)module->task_id, 0);
    }
    if (module->data_base) kfree(module->data_base);
    kfree(module->code_base);
    kfree(module);
    
    module_count--;
    
    serial_puts("Module unloaded: ");
    serial_puts(name);
    serial_puts("\n");
    
    return 0;
}

void kmodule_list(void) {
    serial_puts("Loaded kernel modules:\n");
    
    kmodule_t* current = module_list;
    while (current) {
        serial_puts("  ");
        serial_puts(current->name);
        serial_puts(" v");
        serial_puts(current->version);
        if (current->task_id != 0) {
            serial_puts(" (TID ");
            char tid_buf[12];
            itoa((int)current->task_id, tid_buf, 10);
            serial_puts(tid_buf);
            serial_puts(")");
        }
        
        switch (current->state) {
            case MODULE_LOADED:
                serial_puts(" [LOADED]");
                break;
            case MODULE_LOADING:
                serial_puts(" [LOADING]");
                break;
            case MODULE_UNLOADING:
                serial_puts(" [UNLOADING]");
                break;
            case MODULE_ERROR:
                serial_puts(" [ERROR]");
                break;
            default:
                serial_puts(" [UNKNOWN]");
                break;
        }
        
        serial_puts("\n");
        current = current->next;
    }
    
    if (module_count == 0) {
        serial_puts("  (no modules loaded)\n");
    }
}
