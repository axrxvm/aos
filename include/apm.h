/*
 * === AOS HEADER BEGIN ===
 * ./include/apm.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */

#ifndef APM_H
#define APM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define APM_REPO_BASE_URL "http://repo.aosproject.workers.dev/main/i386" // http cuz https not working as of now.
#define APM_LIST_FILE "/sys/apm/kmodule.list.source"
#define APM_MODULE_DIR "/sys/apm/modules"
#define APM_MAX_MODULES 64
#define APM_MAX_NAME_LEN 64
#define APM_MAX_VERSION_LEN 16
#define APM_MAX_AUTHOR_LEN 64
#define APM_MAX_DESC_LEN 256
#define APM_MAX_LICENSE_LEN 32
#define APM_MAX_FOLDER_LEN 64
#define APM_MAX_MODULE_LEN 64
#define APM_SHA256_LEN 65

typedef struct {
    char name[APM_MAX_NAME_LEN];
    char version[APM_MAX_VERSION_LEN];
    char author[APM_MAX_AUTHOR_LEN];
    char description[APM_MAX_DESC_LEN];
    char license[APM_MAX_LICENSE_LEN];
} apm_metadata_t;

typedef struct {
    char folder[APM_MAX_FOLDER_LEN];
    char module[APM_MAX_MODULE_LEN];
    char sha256[APM_SHA256_LEN];
    apm_metadata_t metadata;
    bool valid;
} apm_module_entry_t;

typedef struct {
    char generated[32];
    apm_module_entry_t modules[APM_MAX_MODULES];
    int module_count;
} apm_repository_t;

// Initialize APM system
void apm_init(void);

// Update repository list from remote
int apm_update(void);

// List available modules
int apm_list_available(void);

// List installed modules
int apm_list_installed(void);

// Show module information
int apm_show_info(const char* module_name);

// Install a module
int apm_install_module(const char* module_name);

// Remove an installed module
int apm_remove_module(const char* module_name);

// Internal helper functions
int apm_load_local_list(apm_repository_t* repo);
int apm_download_list(apm_repository_t* repo);
int apm_save_list(apm_repository_t* repo);
apm_module_entry_t* apm_find_module(const char* module_name);
bool apm_verify_sha256(const uint8_t* data, size_t size, const char* expected_hash);
int apm_download_module(const char* folder, const char* module, uint8_t** data_out, size_t* size_out);

#endif // APM_H
