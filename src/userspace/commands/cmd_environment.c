/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_environment.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <envars.h>

extern void kprint(const char *str);

static void cmd_envars(const char* args) {
    (void)args;
    envar_list();
}

static void cmd_setenv(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: setenv <name>=<value>");
        return;
    }
    
    const char* eq = strchr(args, '=');
    if (!eq) {
        kprint("Error: Use format NAME=VALUE");
        return;
    }
    
    char name[32] = {0};
    char value[128] = {0};
    
    int name_len = eq - args;
    if (name_len >= 32) name_len = 31;
    strncpy(name, args, name_len);
    
    strcpy(value, eq + 1);
    
    if (envar_set(name, value) == 0) {
        kprint("Environment variable set");
    } else {
        kprint("Error: Failed to set variable");
    }
}

static void cmd_getenv(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: getenv <name>");
        return;
    }
    
    const char* value = envar_get(args);
    if (value) {
        vga_puts(args);
        vga_puts("=");
        kprint(value);
    } else {
        kprint("Variable not set");
    }
}

void cmd_module_environment_register(void) {
    command_register_with_category("envars", "", "List environment variables", "Environment", cmd_envars);
    command_register_with_category("setenv", "<name>=<value>", "Set environment variable", "Environment", cmd_setenv);
    command_register_with_category("getenv", "<name>", "Get environment variable", "Environment", cmd_getenv);
}
