/*
 * === AOS HEADER BEGIN ===
 * src/userspace/shell/command_registry.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>

// Command registry - static allocation to avoid early boot issues
static command_t command_registry[MAX_REGISTERED_COMMANDS];
static uint32_t num_registered_commands = 0;

// Forward declarations for built-in command modules
extern void cmd_module_core_register(void);
extern void cmd_module_filesystem_register(void);
extern void cmd_module_memory_register(void);
extern void cmd_module_process_register(void);
extern void cmd_module_partition_register(void);
extern void cmd_module_environment_register(void);
extern void cmd_module_module_register(void);
extern void cmd_module_user_register(void);
extern void cmd_module_init_register(void);
extern void register_security_commands(void);
extern void cmd_module_network_register(void);
extern void cmd_module_apm_register(void);
extern void cmd_module_graphics_register(void);  

// Module VM command execution (from kmodule_v2.c)
extern int execute_module_vm_command(const char* cmd_name, const char* args);

// Command handler helpers (will be defined in module files)
extern void kprint(const char *str);

void command_register(const char* name, const char* syntax, const char* description, command_handler_t handler) {
    command_register_with_category(name, syntax, description, "General", handler);
}

void command_register_with_category(const char* name, const char* syntax, const char* description, const char* category, command_handler_t handler) {
    if (num_registered_commands >= MAX_REGISTERED_COMMANDS) {
        serial_puts("ERROR: Command registry full, cannot register '");
        serial_puts(name);
        serial_puts("'\n");
        return;
    }
    
    command_registry[num_registered_commands].name = name;
    command_registry[num_registered_commands].syntax = syntax;
    command_registry[num_registered_commands].description = description;
    command_registry[num_registered_commands].category = category;
    command_registry[num_registered_commands].handler = handler;
    
    num_registered_commands++;
}

void init_commands(void) {
    serial_puts("Initializing command system...\n");
    num_registered_commands = 0;
    
    // Register all built-in command modules
    cmd_module_core_register();
    cmd_module_filesystem_register();
    cmd_module_memory_register();
    cmd_module_process_register();
    cmd_module_partition_register();
    cmd_module_environment_register();
    cmd_module_module_register();
    cmd_module_user_register();
    cmd_module_init_register();
    register_security_commands();  // v0.7.3
    cmd_module_network_register();  // v0.8.0
    cmd_module_apm_register();  // v0.8.5
    cmd_module_graphics_register();  // v0.8.8 - Enhanced VGA driver
    
    char buf[12];
    serial_puts("Command system initialized with ");
    itoa(num_registered_commands, buf, 10);
    serial_puts(buf);
    serial_puts(" commands.\n");
}

uint32_t command_get_count(void) {
    return num_registered_commands;
}

const command_t* command_get_all(void) {
    return command_registry;
}

int execute_command(const char* input) {
    if (!input || *input == '\0') {
        return 0; // Empty command
    }
    
    // Find the first space to separate command from arguments
    const char* space = input;
    while (*space && *space != ' ') {
        space++;
    }
    
    // Calculate command name length
    uint32_t cmd_len = space - input;
    
    // Skip spaces to find arguments
    const char* args = space;
    while (*args && *args == ' ') {
        args++;
    }
    
    // If no arguments found, set to NULL
    if (*args == '\0') {
        args = NULL;
    }
    
    // Search for command in registry
    for (uint32_t i = 0; i < num_registered_commands; i++) {
        uint32_t name_len = 0;
        const char* name = command_registry[i].name;
        while (name[name_len]) name_len++;
        
        if (cmd_len == name_len && strncmp(input, name, cmd_len) == 0) {
            // Command found, execute handler
            if (command_registry[i].handler) {
                command_registry[i].handler(args);
            } else {
                // Try to execute as module VM command
                char cmd_name_buf[64];
                uint32_t copy_len = cmd_len < 63 ? cmd_len : 63;
                for (uint32_t j = 0; j < copy_len; j++) {
                    cmd_name_buf[j] = input[j];
                }
                cmd_name_buf[copy_len] = '\0';
                
                int vm_result = execute_module_vm_command(cmd_name_buf, args);
                if (vm_result < 0) {
                    // Command exists but execution failed
                    if (vm_result == -1) {
                        kprint("[Error: Module command not found in VM registry]");
                    } else if (vm_result == -2) {
                        kprint("[Error: Module command VM is NULL]");
                    } else if (vm_result == -3) {
                        kprint("[Error: Module command execution limit exceeded]");
                    } else {
                        kprint("[Error: Module command execution failed]");
                    }
                    return -1;
                }
            }
            return 0; // Command found and executed
        }
    }
    
    // Command not found
    vga_puts("Command not found: ");
    
    // Print only the command name, not the arguments
    char temp_buf[64];
    uint32_t copy_len = cmd_len < 63 ? cmd_len : 63;
    for (uint32_t i = 0; i < copy_len; i++) {
        temp_buf[i] = input[i];
    }
    temp_buf[copy_len] = '\0';
    kprint(temp_buf);
    return -1; // Command not found
}
