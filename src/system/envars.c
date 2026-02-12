/*
 * === AOS HEADER BEGIN ===
 * src/system/envars.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <envars.h>
#include <string.h>
#include <serial.h>
#include <syscall.h>
#include <vga.h>

static envar_t global_envars[MAX_ENVARS];
static int envar_count = 0;

void envars_init(void) {
    serial_puts("Initializing environment variables...\n");
    
    memset(global_envars, 0, sizeof(global_envars));
    envar_count = 0;
    
    // Set default environment variables
    envar_set("HOME", "/home");
    envar_set("PATH", "/bin:/usr/bin");
    envar_set("SHELL", "/bin/aosh");  // aOS Shell
    envar_set("TERM", "aos-vga");
    envar_set("USER", "root");
    envar_set("PWD", "/");
    
    serial_puts("Environment variables initialized.\n");
}

const char* envar_get(const char* name) {
    if (!name) return NULL;
    
    for (int i = 0; i < MAX_ENVARS; i++) {
        if (global_envars[i].is_set && 
            strcmp(global_envars[i].name, name) == 0) {
            return global_envars[i].value;
        }
    }
    
    return NULL;  // Not found
}

int envar_set(const char* name, const char* value) {
    if (!name || !value) return -1;
    
    // Check if already exists
    for (int i = 0; i < MAX_ENVARS; i++) {
        if (global_envars[i].is_set &&
            strcmp(global_envars[i].name, name) == 0) {
            // Update existing
            strncpy(global_envars[i].value, value, ENVAR_VALUE_LEN - 1);
            global_envars[i].value[ENVAR_VALUE_LEN - 1] = '\0';
            return 0;
        }
    }
    
    // Find free slot
    for (int i = 0; i < MAX_ENVARS; i++) {
        if (!global_envars[i].is_set) {
            strncpy(global_envars[i].name, name, ENVAR_NAME_LEN - 1);
            global_envars[i].name[ENVAR_NAME_LEN - 1] = '\0';
            strncpy(global_envars[i].value, value, ENVAR_VALUE_LEN - 1);
            global_envars[i].value[ENVAR_VALUE_LEN - 1] = '\0';
            global_envars[i].is_set = 1;
            envar_count++;
            return 0;
        }
    }
    
    return -1;  // No space
}

int envar_unset(const char* name) {
    if (!name) return -1;
    
    for (int i = 0; i < MAX_ENVARS; i++) {
        if (global_envars[i].is_set &&
            strcmp(global_envars[i].name, name) == 0) {
            global_envars[i].is_set = 0;
            envar_count--;
            return 0;
        }
    }
    
    return -1;  // Not found
}

void envar_list(void) {
    vga_puts("Environment Variables:\n");
    
    for (int i = 0; i < MAX_ENVARS; i++) {
        if (global_envars[i].is_set) {
            vga_puts(global_envars[i].name);
            vga_puts("=");
            vga_puts(global_envars[i].value);
            vga_puts("\n");
        }
    }
}

int envar_load_from_file(const char* path) {
    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    
    char buffer[256];
    int bytes_read = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);
    
    if (bytes_read <= 0) {
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    
    // Parse line by line (name=value format)
    char* line = buffer;
    while (*line) {
        // Skip whitespace
        while (*line == ' ' || *line == '\t') line++;
        
        // Skip comments
        if (*line == '#') {
            while (*line && *line != '\n') line++;
            if (*line == '\n') line++;
            continue;
        }
        
        // Parse name=value
        char name[ENVAR_NAME_LEN] = {0};
        char value[ENVAR_VALUE_LEN] = {0};
        int name_len = 0;
        int value_len = 0;
        
        while (*line && *line != '=' && *line != '\n' && name_len < ENVAR_NAME_LEN - 1) {
            name[name_len++] = *line++;
        }
        name[name_len] = '\0';
        
        if (*line == '=') {
            line++;  // Skip '='
            while (*line && *line != '\n' && value_len < ENVAR_VALUE_LEN - 1) {
                value[value_len++] = *line++;
            }
            value[value_len] = '\0';
        }
        
        if (name_len > 0 && value_len > 0) {
            envar_set(name, value);
        }
        
        if (*line == '\n') line++;
    }
    
    return 0;
}

int envar_save_to_file(const char* path) {
    int fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return -1;
    }
    
    char buffer[256];
    for (int i = 0; i < MAX_ENVARS; i++) {
        if (global_envars[i].is_set) {
            strcpy(buffer, global_envars[i].name);
            strcat(buffer, "=");
            strcat(buffer, global_envars[i].value);
            strcat(buffer, "\n");
            sys_write(fd, buffer, strlen(buffer));
        }
    }
    
    sys_close(fd);
    return 0;
}

int load_startup_script(const char* username) {
    if (!username) return -1;
    
    // Build path to user's .aosrc file
    char path[128];
    strcpy(path, "/home/");
    strcat(path, username);
    strcat(path, "/.aosrc");
    
    serial_puts("Loading startup script: ");
    serial_puts(path);
    serial_puts("\n");
    
    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        serial_puts("No startup script found\n");
        return -1;
    }
    
    char buffer[512];
    int bytes_read = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);
    
    if (bytes_read <= 0) {
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    
    // Execute commands from startup script
    char* line = buffer;
    while (*line) {
        // Skip whitespace and comments
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#') {
            while (*line && *line != '\n') line++;
            if (*line == '\n') line++;
            continue;
        }
        
        // Extract command line
        char cmd[256] = {0};
        int cmd_len = 0;
        while (*line && *line != '\n' && cmd_len < 255) {
            cmd[cmd_len++] = *line++;
        }
        cmd[cmd_len] = '\0';
        
        if (cmd_len > 0) {
            // Check for envar set command (set NAME=VALUE)
            if (strncmp(cmd, "set ", 4) == 0) {
                char* eq = strchr(cmd + 4, '=');
                if (eq) {
                    *eq = '\0';
                    envar_set(cmd + 4, eq + 1);
                }
            }
        }
        
        if (*line == '\n') line++;
    }
    
    serial_puts("Startup script loaded\n");
    return 0;
}
