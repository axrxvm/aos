/*
 * === AOS HEADER BEGIN ===
 * include/command_registry.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include <stdint.h>

// Command handler function type
typedef void (*command_handler_t)(const char* args);

// Command structure
typedef struct {
    const char* name;
    const char* syntax;
    const char* description;
    const char* category;  // New field for categorization
    command_handler_t handler;
} command_t;

// Maximum number of commands that can be registered
#define MAX_REGISTERED_COMMANDS 256

// Command registry functions (to be called by command modules)
void command_register(const char* name, const char* syntax, const char* description, command_handler_t handler);
void command_register_with_category(const char* name, const char* syntax, const char* description, const char* category, command_handler_t handler);

// Command system initialization and execution
void init_commands(void);
int execute_command(const char* input);

// Get command registry info
uint32_t command_get_count(void);
const command_t* command_get_all(void);

#endif // COMMAND_REGISTRY_H
