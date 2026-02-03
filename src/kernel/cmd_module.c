/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/cmd_module.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <kmodule.h>
#include <kmodule_api.h>
#include <syscall.h>
#include <vmm.h>

extern void kprint(const char *str);
extern uint32_t kernel_get_version(void);

static void cmd_modlist(const char* args) {
    (void)args;
    kmodule_list();
    kprint("--- V2 Modules ---");
    kmodule_list_v2();
}

static void cmd_modload(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: modload <path_to_akm_file>");
        return;
    }
    
    // Open the module file
    int fd = sys_open(args, O_RDONLY);
    if (fd < 0) {
        kprint("Error: Failed to open module file");
        return;
    }
    
    // Read magic bytes to detect version
    uint32_t magic;
    if (sys_read(fd, &magic, sizeof(magic)) != sizeof(magic)) {
        sys_close(fd);
        kprint("Error: Failed to read module header");
        return;
    }
    
    // Reset file position
    sys_lseek(fd, 0, SEEK_SET);
    
    // Check if v2 module
    if (magic == AKM_MAGIC_V2) {
        // V2 module - read entire file into memory
        kprint("Detected v2 module, loading...");
        
        // Get file size
        int original_pos = sys_lseek(fd, 0, SEEK_CUR);
        int file_size = sys_lseek(fd, 0, SEEK_END);
        sys_lseek(fd, original_pos, SEEK_SET);
        
        if (file_size <= 0) {
            sys_close(fd);
            kprint("Error: Invalid file size");
            return;
        }
        
        // Allocate buffer for entire file
        void* file_data = kmalloc(file_size);
        if (!file_data) {
            sys_close(fd);
            kprint("Error: Failed to allocate memory for module");
            return;
        }
        
        // Read entire file
        if (sys_read(fd, file_data, file_size) != file_size) {
            kfree(file_data);
            sys_close(fd);
            kprint("Error: Failed to read module data");
            return;
        }
        
        sys_close(fd);
        
        // Load v2 module
        int result = kmodule_load_v2(file_data, file_size);
        kfree(file_data);
        
        if (result == 0) {
            kprint("Module loaded successfully");
        } else {
            kprint("Error: Failed to load v2 module");
        }
    } else {
        // V1 module - use old loader
        sys_close(fd);
        kprint("Detected v1 module, loading...");
        
        if (kmodule_load(args) == 0) {
            kprint("Module loaded successfully");
        } else {
            kprint("Error: Failed to load module");
        }
    }
}

static void cmd_modunload(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: modunload <module_name>");
        return;
    }
    
    // Try v2 first
    if (kmodule_unload_v2(args) == 0) {
        kprint("V2 module unloaded successfully");
        return;
    }
    
    // Try v1
    if (kmodule_unload(args) == 0) {
        kprint("Module unloaded successfully");
    } else {
        kprint("Error: Failed to unload module (not found)");
    }
}

static void cmd_kernelver(const char* args) {
    (void)args;
    
    uint32_t ver = kernel_get_version();
    uint32_t major = (ver >> 16) & 0xFF;
    uint32_t minor = (ver >> 8) & 0xFF;
    uint32_t patch = ver & 0xFF;
    
    char line[64];
    char num_str[8];
    
    strcpy(line, "Kernel version: ");
    itoa(major, num_str, 10);
    strcat(line, num_str);
    strcat(line, ".");
    itoa(minor, num_str, 10);
    strcat(line, num_str);
    strcat(line, ".");
    itoa(patch, num_str, 10);
    strcat(line, num_str);
    
    kprint(line);
    kprint("Module format: .akm (aOS Kernel Module)");
}

void cmd_module_module_register(void) {
    command_register_with_category("modlist", "", "List kernel modules", "Modules", cmd_modlist);
    command_register_with_category("modload", "<path>", "Load kernel module", "Modules", cmd_modload);
    command_register_with_category("modunload", "<name>", "Unload kernel module", "Modules", cmd_modunload);
    command_register_with_category("kernelver", "", "Display kernel version", "Modules", cmd_kernelver);
}
