/*
 * === AOS HEADER BEGIN ===
 * ./include/envars.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef ENVARS_H
#define ENVARS_H

#include <stdint.h>

// Environment variables
#define MAX_ENVARS 64
#define ENVAR_NAME_LEN 32
#define ENVAR_VALUE_LEN 128

typedef struct {
    char name[ENVAR_NAME_LEN];
    char value[ENVAR_VALUE_LEN];
    int is_set;
} envar_t;

// Initialize environment variables
void envars_init(void);

// Get/Set environment variables
const char* envar_get(const char* name);
int envar_set(const char* name, const char* value);
int envar_unset(const char* name);

// List all environment variables
void envar_list(void);

// Load/Save environment from file
int envar_load_from_file(const char* path);
int envar_save_to_file(const char* path);

// Load user startup script (.aosrc)
int load_startup_script(const char* username);

#endif // ENVARS_H
