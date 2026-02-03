/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/apm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */

#include <apm.h>
#include <string.h>
#include <stdlib.h>
#include <vga.h>
#include <serial.h>
#include <net/http.h>
#include <crypto/sha256.h>
#include <fs/vfs.h>
#include <kmodule.h>
#include <vmm.h>

static apm_repository_t g_apm_repo;
static bool g_apm_initialized = false;

// Simple JSON parsing helpers (minimal asf)
static char* json_find_field(const char* json, const char* field) {
    if (!json || !field) return NULL;
    
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", field);
    char* pos = strstr(json, search);
    if (!pos) return NULL;
    
    pos = strchr(pos, ':');
    if (!pos) return NULL;
    
    // Skip whitespace and quotes
    pos++;
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
    if (*pos == '"') pos++;
    
    return pos;
}

static int json_extract_string(const char* start, char* out, size_t max_len) {
    const char* end = strchr(start, '"');
    if (!end) return -1;
    
    size_t len = end - start;
    if (len >= max_len) len = max_len - 1;
    
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int json_parse_module(const char* json_module, apm_module_entry_t* entry) {
    char* pos;
    
    // Parse folder
    pos = json_find_field(json_module, "folder");
    if (!pos || json_extract_string(pos, entry->folder, APM_MAX_FOLDER_LEN) < 0) {
        return -1;
    }
    
    // Parse module
    pos = json_find_field(json_module, "module");
    if (!pos || json_extract_string(pos, entry->module, APM_MAX_MODULE_LEN) < 0) {
        return -1;
    }
    
    // Parse sha256
    pos = json_find_field(json_module, "sha256");
    if (!pos || json_extract_string(pos, entry->sha256, APM_SHA256_LEN) < 0) {
        return -1;
    }
    
    // Parse metadata
    char* metadata_start = strstr(json_module, "\"metadata\"");
    if (metadata_start) {
        pos = json_find_field(metadata_start, "name");
        if (pos) json_extract_string(pos, entry->metadata.name, APM_MAX_NAME_LEN);
        
        pos = json_find_field(metadata_start, "version");
        if (pos) json_extract_string(pos, entry->metadata.version, APM_MAX_VERSION_LEN);
        
        pos = json_find_field(metadata_start, "author");
        if (pos) json_extract_string(pos, entry->metadata.author, APM_MAX_AUTHOR_LEN);
        
        pos = json_find_field(metadata_start, "description");
        if (pos) json_extract_string(pos, entry->metadata.description, APM_MAX_DESC_LEN);
        
        pos = json_find_field(metadata_start, "license");
        if (pos) json_extract_string(pos, entry->metadata.license, APM_MAX_LICENSE_LEN);
    }
    
    entry->valid = true;
    return 0;
}

// Find the MATCHING closing brace for a JSON object (handles nesting)
static char* json_find_object_end(const char* start) {
    if (!start || *start != '{') return NULL;
    
    const char* p = start + 1;
    int depth = 1;
    bool in_string = false;
    bool escape = false;
    
    while (*p && depth > 0) {
        if (escape) {
            escape = false;
            p++;
            continue;
        }
        
        if (*p == '\\' && in_string) {
            escape = true;
            p++;
            continue;
        }
        
        if (*p == '"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
        }
        
        p++;
    }
    
    return (depth == 0) ? (char*)(p - 1) : NULL;
}

// Zero a module entry safely (field by field, no huge memset)
static void zero_module_entry(apm_module_entry_t* entry) {
    if (!entry) return;
    
    // Zero each field individually
    for (size_t i = 0; i < APM_MAX_FOLDER_LEN; i++) entry->folder[i] = 0;
    for (size_t i = 0; i < APM_MAX_MODULE_LEN; i++) entry->module[i] = 0;
    for (size_t i = 0; i < APM_SHA256_LEN; i++) entry->sha256[i] = 0;
    
    // Zero metadata
    for (size_t i = 0; i < APM_MAX_NAME_LEN; i++) entry->metadata.name[i] = 0;
    for (size_t i = 0; i < APM_MAX_VERSION_LEN; i++) entry->metadata.version[i] = 0;
    for (size_t i = 0; i < APM_MAX_AUTHOR_LEN; i++) entry->metadata.author[i] = 0;
    for (size_t i = 0; i < APM_MAX_DESC_LEN; i++) entry->metadata.description[i] = 0;
    for (size_t i = 0; i < APM_MAX_LICENSE_LEN; i++) entry->metadata.license[i] = 0;
    
    entry->valid = false;
}

// Zero the repository safely (field by field)
static void zero_repository(apm_repository_t* repo) {
    if (!repo) return;
    
    serial_puts("[APM] Zeroing repository structure...\n");
    
    // Zero generated field
    for (size_t i = 0; i < sizeof(repo->generated); i++) {
        repo->generated[i] = 0;
    }
    
    // Zero each module entry
    for (int i = 0; i < APM_MAX_MODULES; i++) {
        zero_module_entry(&repo->modules[i]);
    }
    
    repo->module_count = 0;
    
    serial_puts("[APM] Repository zeroed successfully\n");
}

static int json_parse_list(const char* json, apm_repository_t* repo) {
    if (!json || !repo) {
        serial_puts("[APM] json_parse_list: NULL parameter\n");
        return -1;
    }
    
    serial_puts("[APM] Starting json_parse_list\n");
    
    // Zero structure safely (no huge memset)
    zero_repository(repo);
    
    // Parse generated timestamp
    char* pos = json_find_field(json, "generated");
    if (pos) {
        json_extract_string(pos, repo->generated, sizeof(repo->generated));
        serial_puts("[APM] Generated: ");
        serial_puts(repo->generated);
        serial_puts("\n");
    }
    
    // Find modules array
    char* modules_start = strstr(json, "\"modules\"");
    if (!modules_start) {
        serial_puts("[APM] No modules array found\n");
        return -1;
    }
    
    modules_start = strchr(modules_start, '[');
    if (!modules_start) {
        serial_puts("[APM] No [ found after modules\n");
        return -1;
    }
    
    serial_puts("[APM] Found modules array\n");
    
    // Parse each module entry
    char* module_start = strchr(modules_start, '{');
    repo->module_count = 0;
    
    while (module_start && repo->module_count < APM_MAX_MODULES) {
        // Find the CORRECT closing brace (handle nested objects like metadata)
        char* module_end = json_find_object_end(module_start);
        if (!module_end) {
            serial_puts("[APM] Could not find matching } for module\n");
            break;
        }
        
        // Create temporary string for this module
        size_t module_len = module_end - module_start + 1;
        
        // Sanity check
        if (module_len > 4096) {
            serial_puts("[APM] Module JSON too large, skipping\n");
            module_start = strchr(module_end + 1, '{');
            continue;
        }
        
        serial_puts("[APM] Parsing module ");
        char idx_str[16];
        itoa(repo->module_count, idx_str, 10);
        serial_puts(idx_str);
        serial_puts(", len=");
        itoa(module_len, idx_str, 10);
        serial_puts(idx_str);
        serial_puts("\n");
        
        char* module_json = (char*)kmalloc(module_len + 1);
        if (!module_json) {
            serial_puts("[APM] kmalloc failed for module_json\n");
            break;
        }
        
        // Copy byte by byte (safer than memcpy for small data)
        for (size_t i = 0; i < module_len; i++) {
            module_json[i] = module_start[i];
        }
        module_json[module_len] = '\0';
        
        if (json_parse_module(module_json, &repo->modules[repo->module_count]) == 0) {
            repo->module_count++;
            serial_puts("[APM] Module parsed OK: ");
            serial_puts(repo->modules[repo->module_count - 1].metadata.name);
            serial_puts("\n");
        } else {
            serial_puts("[APM] json_parse_module failed\n");
        }
        
        kfree(module_json);
        
        // Find next module (after this object's closing brace)
        module_start = strchr(module_end + 1, '{');
    }
    
    serial_puts("[APM] Parsing complete, module_count=");
    char cnt_str[16];
    itoa(repo->module_count, cnt_str, 10);
    serial_puts(cnt_str);
    serial_puts("\n");
    
    return 0;
}

void apm_init(void) {
    if (g_apm_initialized) return;
    
    serial_puts("[APM] Initializing aOS Package Manager\n");
    
    // Ensure APM directories exist (use /sys/apm, not /dev/apm which gets overlaid by devfs)
    vfs_mkdir("/sys");
    vfs_mkdir("/sys/apm");
    vfs_mkdir(APM_MODULE_DIR);
    
    // Try to load cached list
    if (apm_load_local_list(&g_apm_repo) == 0) {
        serial_puts("[APM] Loaded cached repository list\n");
    } else {
        serial_puts("[APM] No cached list found, run 'apm update' to download\n");
    }
    
    g_apm_initialized = true;
}

int apm_load_local_list(apm_repository_t* repo) {
    int fd = vfs_open(APM_LIST_FILE, VFS_FILE);
    if (fd < 0) {
        return -1;
    }
    
    // Get file size using stat
    stat_t st;
    if (vfs_stat(APM_LIST_FILE, &st) < 0) {
        vfs_close(fd);
        return -1;
    }
    
    size_t size = st.st_size;
    char* json_data = (char*)kmalloc(size + 1);
    if (!json_data) {
        vfs_close(fd);
        return -1;
    }
    
    // Read file
    if (vfs_read(fd, (uint8_t*)json_data, size) != (int)size) {
        kfree(json_data);
        vfs_close(fd);
        return -1;
    }
    
    json_data[size] = '\0';
    vfs_close(fd);
    
    // Parse JSON
    int result = json_parse_list(json_data, repo);
    kfree(json_data);
    
    return result;
}

int apm_save_list(apm_repository_t* repo) {
    (void)repo; // For now, we just save the raw JSON we downloaded
    // This function is called after download, the data is already written
    return 0;
}

int apm_download_list(apm_repository_t* repo) {
    char url[256];
    snprintf(url, sizeof(url), "%s/kmodule/list.json", APM_REPO_BASE_URL);
    
    vga_puts("[APM] Downloading repository list...\n");
    serial_puts("[APM] Downloading from: ");
    serial_puts(url);
    serial_puts("\n");
    
    http_response_t* response = http_response_create();
    if (!response) {
        vga_puts("[APM] Error: Failed to create HTTP response\n");
        return -1;
    }
    
    int result = http_get(url, response);
    if (result < 0 || response->status_code != HTTP_STATUS_OK) {
        vga_puts("[APM] Error: Failed to download list (HTTP ");
        char code_str[16];
        itoa(response->status_code, code_str, 10);
        vga_puts(code_str);
        vga_puts(")\n");
        http_response_free(response);
        return -1;
    }
    
    if (!response->body || response->body_len == 0) {
        vga_puts("[APM] Error: Empty response\n");
        http_response_free(response);
        return -1;
    }
    
    // Sanity check - body length should be reasonable
    if (response->body_len > 1024 * 1024) {  // 1MB max
        vga_puts("[APM] Error: Response too large\n");
        http_response_free(response);
        return -1;
    }
    
    // Parse the JSON
    char* json_data = (char*)kmalloc(response->body_len + 1);
    if (!json_data) {
        vga_puts("[APM] Error: Out of memory\n");
        http_response_free(response);
        return -1;
    }
    
    memcpy(json_data, response->body, response->body_len);
    json_data[response->body_len] = '\0';
    
    result = json_parse_list(json_data, repo);
    
    if (result == 0) {
        // Save to disk
        int fd = vfs_open(APM_LIST_FILE, O_WRONLY | O_CREAT | O_TRUNC);
        
        if (fd >= 0) {
            vfs_write(fd, response->body, response->body_len);
            vfs_close(fd);
            vga_puts("[APM] Repository list updated successfully\n");
        } else {
            vga_puts("[APM] Warning: Could not save list to disk\n");
        }
    } else {
        vga_puts("[APM] Error: Failed to parse repository list\n");
    }
    
    kfree(json_data);
    http_response_free(response);
    
    // NOTE: Caller is responsible for copying to global if needed
    // We removed the memcpy here to avoid 40KB copy issues
    
    return result;
}

int apm_update(void) {
    serial_puts("[APM] apm_update called\n");
    
    // Use dynamically allocated repository to avoid large stack allocation
    apm_repository_t* repo = (apm_repository_t*)kmalloc(sizeof(apm_repository_t));
    if (!repo) {
        vga_puts("[APM] Error: Out of memory\n");
        return -1;
    }
    
    serial_puts("[APM] Allocated repository struct\n");
    
    int result = apm_download_list(repo);
    
    if (result == 0) {
        // Copy to global - field by field to be safe
        for (size_t i = 0; i < sizeof(g_apm_repo.generated); i++) {
            g_apm_repo.generated[i] = repo->generated[i];
        }
        g_apm_repo.module_count = repo->module_count;
        for (int i = 0; i < repo->module_count && i < APM_MAX_MODULES; i++) {
            // Copy each module entry byte by byte
            char* dst = (char*)&g_apm_repo.modules[i];
            char* src = (char*)&repo->modules[i];
            for (size_t j = 0; j < sizeof(apm_module_entry_t); j++) {
                dst[j] = src[j];
            }
        }
        serial_puts("[APM] Copied to global repo\n");
    }
    
    kfree(repo);
    serial_puts("[APM] apm_update complete\n");
    return result;
}

apm_module_entry_t* apm_find_module(const char* module_name) {
    for (int i = 0; i < g_apm_repo.module_count; i++) {
        if (strcmp(g_apm_repo.modules[i].metadata.name, module_name) == 0) {
            return &g_apm_repo.modules[i];
        }
    }
    return NULL;
}

bool apm_verify_sha256(const uint8_t* data, size_t size, const char* expected_hash) {
    uint8_t digest[SHA256_DIGEST_SIZE];
    char computed_hash[SHA256_DIGEST_SIZE * 2 + 1];
    
    // Compute SHA256
    sha256_hash(data, size, digest);
    sha256_to_hex(digest, computed_hash);
    
    // Compare (case-insensitive)
    for (size_t i = 0; i < strlen(expected_hash); i++) {
        char c1 = computed_hash[i];
        char c2 = expected_hash[i];
        
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        
        if (c1 != c2) return false;
    }
    
    return true;
}

int apm_download_module(const char* folder, const char* module, uint8_t** data_out, size_t* size_out) {
    char url[512];
    snprintf(url, sizeof(url), "%s/kmodule/%s/%s", APM_REPO_BASE_URL, folder, module);
    
    serial_puts("[APM] Downloading module from: ");
    serial_puts(url);
    serial_puts("\n");
    
    http_response_t* response = http_response_create();
    if (!response) {
        return -1;
    }
    
    int result = http_get(url, response);
    if (result < 0 || response->status_code != HTTP_STATUS_OK) {
        vga_puts("[APM] Error: Failed to download module (HTTP ");
        char code_str[16];
        itoa(response->status_code, code_str, 10);
        vga_puts(code_str);
        vga_puts(")\n");
        http_response_free(response);
        return -1;
    }
    
    if (!response->body || response->body_len == 0) {
        vga_puts("[APM] Error: Empty module file\n");
        http_response_free(response);
        return -1;
    }
    
    // Allocate buffer for module data
    *data_out = (uint8_t*)kmalloc(response->body_len);
    if (!*data_out) {
        vga_puts("[APM] Error: Out of memory\n");
        http_response_free(response);
        return -1;
    }
    
    memcpy(*data_out, response->body, response->body_len);
    *size_out = response->body_len;
    
    http_response_free(response);
    return 0;
}

int apm_list_available(void) {
    if (g_apm_repo.module_count == 0) {
        vga_puts("[APM] No repository list found. Run 'apm update' first.\n");
        return -1;
    }
    
    vga_puts("\nAvailable Kernel Modules:\n");
    vga_puts("==========================\n");
    
    for (int i = 0; i < g_apm_repo.module_count; i++) {
        if (g_apm_repo.modules[i].valid) {
            vga_puts("  * ");
            vga_puts(g_apm_repo.modules[i].metadata.name);
            vga_puts("\n");
        }
    }
    
    vga_puts("\nUse 'apm kmodule info <name>' for details.\n");
    return 0;
}

int apm_list_installed(void) {
    vga_puts("\nInstalled Kernel Modules:\n");
    vga_puts("=========================\n");
    
    int fd = vfs_open(APM_MODULE_DIR, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        vga_puts("  (none)\n");
        return 0;
    }
    
    int count = 0;
    dirent_t entry;
    
    while (vfs_readdir(fd, &entry) == 0) {
        if (entry.type == VFS_FILE && strstr(entry.name, ".akm")) {
            vga_puts("  * ");
            vga_puts(entry.name);
            vga_puts("\n");
            count++;
        }
    }
    
    vfs_close(fd);
    
    if (count == 0) {
        vga_puts("  (none)\n");
    }
    
    return 0;
}

int apm_show_info(const char* module_name) {
    if (g_apm_repo.module_count == 0) {
        vga_puts("[APM] No repository list found. Run 'apm update' first.\n");
        return -1;
    }
    
    apm_module_entry_t* entry = apm_find_module(module_name);
    if (!entry) {
        vga_puts("[APM] Error: Module '");
        vga_puts(module_name);
        vga_puts("' not found in repository\n");
        return -1;
    }
    
    vga_puts("\nModule Information:\n");
    vga_puts("===================\n");
    vga_puts("Name:        ");
    vga_puts(entry->metadata.name);
    vga_puts("\n");
    
    vga_puts("Version:     ");
    vga_puts(entry->metadata.version);
    vga_puts("\n");
    
    vga_puts("Author:      ");
    vga_puts(entry->metadata.author);
    vga_puts("\n");
    
    vga_puts("License:     ");
    vga_puts(entry->metadata.license);
    vga_puts("\n");
    
    vga_puts("Description: ");
    vga_puts(entry->metadata.description);
    vga_puts("\n");
    
    vga_puts("\nFile:        ");
    vga_puts(entry->module);
    vga_puts("\n");
    
    vga_puts("SHA256:      ");
    vga_puts(entry->sha256);
    vga_puts("\n");
    
    return 0;
}

int apm_install_module(const char* module_name) {
    if (g_apm_repo.module_count == 0) {
        vga_puts("[APM] No repository list found. Run 'apm update' first.\n");
        return -1;
    }
    
    apm_module_entry_t* entry = apm_find_module(module_name);
    if (!entry) {
        vga_puts("[APM] Error: Module '");
        vga_puts(module_name);
        vga_puts("' not found in repository\n");
        return -1;
    }
    
    vga_puts("[APM] Installing module: ");
    vga_puts(module_name);
    vga_puts("\n");
    
    // Download module
    uint8_t* module_data = NULL;
    size_t module_size = 0;
    
    if (apm_download_module(entry->folder, entry->module, &module_data, &module_size) < 0) {
        return -1;
    }
    
    vga_puts("[APM] Downloaded ");
    char size_str[32];
    itoa(module_size, size_str, 10);
    vga_puts(size_str);
    vga_puts(" bytes\n");
    
    // Verify SHA256
    vga_puts("[APM] Verifying integrity...\n");
    if (!apm_verify_sha256(module_data, module_size, entry->sha256)) {
        vga_puts("[APM] Error: SHA256 verification failed!\n");
        vga_puts("[APM] Expected: ");
        vga_puts(entry->sha256);
        vga_puts("\n");
        kfree(module_data);
        return -1;
    }
    
    vga_puts("[APM] Verification passed\n");
    
    // Save to disk
    char module_path[256];
    snprintf(module_path, sizeof(module_path), "%s/%s", APM_MODULE_DIR, entry->module);
    
    int fd = vfs_open(module_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        vga_puts("[APM] Error: Failed to create module file\n");
        kfree(module_data);
        return -1;
    }
    
    if (vfs_write(fd, module_data, module_size) != (int)module_size) {
        vga_puts("[APM] Error: Failed to write module file\n");
        vfs_close(fd);
        kfree(module_data);
        return -1;
    }
    
    vfs_close(fd);
    kfree(module_data);
    
    vga_puts("[APM] Module installed successfully to: ");
    vga_puts(module_path);
    vga_puts("\n");
    vga_puts("[APM] Use 'modload ");
    vga_puts(module_path);
    vga_puts("' to load it\n");
    
    return 0;
}

int apm_remove_module(const char* module_name) {
    char module_path[256];
    
    // Try with .akm extension
    snprintf(module_path, sizeof(module_path), "%s/%s", APM_MODULE_DIR, module_name);
    if (!strstr(module_name, ".akm")) {
        strcat(module_path, ".akm");
    }
    
    // Check if file exists
    int fd = vfs_open(module_path, O_RDONLY);
    if (fd < 0) {
        vga_puts("[APM] Error: Module '");
        vga_puts(module_name);
        vga_puts("' is not installed\n");
        return -1;
    }
    vfs_close(fd);
    
    // Unload if loaded (try both with and without path)
    kmodule_unload(module_path);
    kmodule_unload(module_name);
    
    // Remove file
    if (vfs_unlink(module_path) < 0) {
        vga_puts("[APM] Error: Failed to remove module file\n");
        return -1;
    }
    
    vga_puts("[APM] Module '");
    vga_puts(module_name);
    vga_puts("' removed successfully\n");
    
    return 0;
}
