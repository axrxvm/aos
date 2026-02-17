/*
 * === AOS HEADER BEGIN ===
 * src/system/apm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
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
#include <kmodule_api.h>
#include <vmm.h>

static apm_repository_t g_apm_repo;
static bool g_apm_initialized = false;

#define APM_AUTOLOAD_MAX_BYTES 4096
#define APM_V1_MAGIC 0x004D4B41

static int apm_string_has_suffix(const char* str, const char* suffix) {
    if (!str || !suffix) return 0;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static const char* apm_basename(const char* path) {
    if (!path) return NULL;
    const char* slash = strrchr(path, '/');
    return slash ? (slash + 1) : path;
}

static int apm_normalize_module_name(const char* module_name, char* out, size_t out_size) {
    if (!module_name || !out || out_size == 0) {
        return -1;
    }

    const char* base = apm_basename(module_name);
    if (!base || base[0] == '\0') {
        return -1;
    }

    size_t len = strlen(base);
    if (len >= out_size) {
        len = out_size - 1;
    }

    memcpy(out, base, len);
    out[len] = '\0';

    if (apm_string_has_suffix(out, ".akm")) {
        out[strlen(out) - 4] = '\0';
    }

    return out[0] == '\0' ? -1 : 0;
}

static int apm_build_module_path(const char* module_name, char* module_path, size_t path_size) {
    if (!module_name || !module_path || path_size == 0) {
        return -1;
    }

    if (module_name[0] == '/') {
        if (strlen(module_name) >= path_size) {
            return -1;
        }
        strcpy(module_path, module_name);
        return 0;
    }

    int written = snprintf(module_path, path_size, "%s/%s", APM_MODULE_DIR, module_name);
    if (written < 0 || (size_t)written >= path_size) {
        return -1;
    }

    if (!apm_string_has_suffix(module_path, ".akm")) {
        if (strlen(module_path) + 4 >= path_size) {
            return -1;
        }
        strcat(module_path, ".akm");
    }

    return 0;
}

static int apm_load_module_internal(const char* module_name, bool to_vga);

static int apm_read_module_blob(const char* module_path, uint8_t** data_out, size_t* size_out) {
    if (!module_path || !data_out || !size_out) {
        return -1;
    }

    stat_t st;
    if (vfs_stat(module_path, &st) < 0 || st.st_size == 0) {
        return -1;
    }

    int fd = vfs_open(module_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    *data_out = (uint8_t*)kmalloc(st.st_size);
    if (!*data_out) {
        vfs_close(fd);
        return -1;
    }

    if (vfs_read(fd, *data_out, st.st_size) != (int)st.st_size) {
        kfree(*data_out);
        *data_out = NULL;
        vfs_close(fd);
        return -1;
    }

    vfs_close(fd);
    *size_out = st.st_size;
    return 0;
}

static int apm_get_module_identity(const char* module_path, char* module_id, size_t module_id_size, bool* is_v2_out) {
    if (!module_path || !module_id || module_id_size == 0) {
        return -1;
    }

    int fd = vfs_open(module_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    uint32_t magic = 0;
    if (vfs_read(fd, &magic, sizeof(magic)) != sizeof(magic)) {
        vfs_close(fd);
        return -1;
    }

    if (magic == AKM_MAGIC_V2) {
        akm_header_v2_t hdr;
        vfs_lseek(fd, 0, SEEK_SET);
        if (vfs_read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
            vfs_close(fd);
            return -1;
        }
        strncpy(module_id, hdr.name, module_id_size - 1);
        module_id[module_id_size - 1] = '\0';
        if (is_v2_out) *is_v2_out = true;
    } else if (magic == APM_V1_MAGIC) {
        akm_header_t hdr;
        vfs_lseek(fd, 0, SEEK_SET);
        if (vfs_read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
            vfs_close(fd);
            return -1;
        }
        strncpy(module_id, hdr.name, module_id_size - 1);
        module_id[module_id_size - 1] = '\0';
        if (is_v2_out) *is_v2_out = false;
    } else {
        vfs_close(fd);
        return -1;
    }

    vfs_close(fd);
    return module_id[0] == '\0' ? -1 : 0;
}

static int apm_read_autoload_entries(char entries[][APM_MAX_NAME_LEN], int max_entries) {
    if (!entries || max_entries <= 0) return -1;

    int fd = vfs_open(APM_AUTOLOAD_FILE, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    stat_t st;
    if (vfs_stat(APM_AUTOLOAD_FILE, &st) < 0 || st.st_size == 0) {
        vfs_close(fd);
        return 0;
    }

    size_t size = st.st_size;
    if (size > APM_AUTOLOAD_MAX_BYTES) {
        size = APM_AUTOLOAD_MAX_BYTES;
    }

    char* data = (char*)kmalloc(size + 1);
    if (!data) {
        vfs_close(fd);
        return -1;
    }

    int bytes_read = vfs_read(fd, data, size);
    vfs_close(fd);
    if (bytes_read <= 0) {
        kfree(data);
        return 0;
    }
    data[bytes_read] = '\0';

    int count = 0;
    char* p = data;
    while (*p && count < max_entries) {
        while (*p == '\n' || *p == '\r') p++;
        if (!*p) break;

        char line[APM_MAX_NAME_LEN];
        int line_len = 0;
        while (*p && *p != '\n' && *p != '\r') {
            if (line_len < APM_MAX_NAME_LEN - 1) {
                line[line_len++] = *p;
            }
            p++;
        }
        line[line_len] = '\0';

        int start = 0;
        while (line[start] == ' ' || line[start] == '\t') start++;
        while (line_len > start && (line[line_len - 1] == ' ' || line[line_len - 1] == '\t')) {
            line[--line_len] = '\0';
        }

        if (line[start] != '\0') {
            char normalized[APM_MAX_NAME_LEN];
            if (apm_normalize_module_name(line + start, normalized, sizeof(normalized)) == 0) {
                bool exists = false;
                for (int i = 0; i < count; i++) {
                    if (strcmp(entries[i], normalized) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    strncpy(entries[count], normalized, APM_MAX_NAME_LEN - 1);
                    entries[count][APM_MAX_NAME_LEN - 1] = '\0';
                    count++;
                }
            }
        }
    }

    kfree(data);
    return count;
}

static int apm_write_autoload_entries(char entries[][APM_MAX_NAME_LEN], int count) {
    if (count <= 0) {
        vfs_unlink(APM_AUTOLOAD_FILE);
        return 0;
    }

    int fd = vfs_open(APM_AUTOLOAD_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (entries[i][0] == '\0') continue;
        size_t len = strlen(entries[i]);
        if (vfs_write(fd, entries[i], len) != (int)len || vfs_write(fd, "\n", 1) != 1) {
            vfs_close(fd);
            return -1;
        }
    }

    vfs_close(fd);
    return 0;
}

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
    // Check if directories already exist to avoid recreating on each boot
    if (!vfs_resolve_path("/sys")) {
        vfs_mkdir("/sys");
    }
    if (!vfs_resolve_path("/sys/apm")) {
        vfs_mkdir("/sys/apm");
    }
    if (!vfs_resolve_path(APM_MODULE_DIR)) {
        vfs_mkdir(APM_MODULE_DIR);
    }
    
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

static int apm_load_module_internal(const char* module_name, bool to_vga) {
    char module_path[256];
    bool is_v2 = false;
    char module_id[MODULE_NAME_LEN];
    module_id[0] = '\0';

    if (apm_build_module_path(module_name, module_path, sizeof(module_path)) < 0) {
        if (to_vga) vga_puts("[APM] Error: Invalid module name/path\n");
        else serial_puts("[APM] Error: Invalid module name/path\n");
        return -1;
    }

    if (apm_get_module_identity(module_path, module_id, sizeof(module_id), &is_v2) < 0) {
        if (to_vga) {
            vga_puts("[APM] Error: Invalid module file: ");
            vga_puts(module_path);
            vga_puts("\n");
        } else {
            serial_puts("[APM] Error: Invalid module file: ");
            serial_puts(module_path);
            serial_puts("\n");
        }
        return -1;
    }

    int result = 0;
    if (is_v2) {
        uint8_t* data = NULL;
        size_t size = 0;

        if (apm_read_module_blob(module_path, &data, &size) < 0) {
            if (to_vga) vga_puts("[APM] Error: Failed to read module file\n");
            else serial_puts("[APM] Error: Failed to read module file\n");
            return -1;
        }

        result = kmodule_load_v2(data, size);
        kfree(data);
    } else {
        result = kmodule_load(module_path);
    }

    if (result == 0) {
        if (to_vga) {
            vga_puts("[APM] Loaded module: ");
            if (module_id[0] != '\0') {
                vga_puts(module_id);
            } else {
                vga_puts(module_name);
            }
            vga_puts("\n");
        } else {
            serial_puts("[APM] Loaded module: ");
            if (module_id[0] != '\0') {
                serial_puts(module_id);
            } else {
                serial_puts(module_name);
            }
            serial_puts("\n");
        }
    } else {
        if (to_vga) vga_puts("[APM] Error: Failed to load module\n");
        else serial_puts("[APM] Error: Failed to load module\n");
    }

    return result;
}

int apm_load_module(const char* module_name) {
    return apm_load_module_internal(module_name, true);
}

int apm_unload_module(const char* module_name) {
    if (!module_name || module_name[0] == '\0') {
        vga_puts("[APM] Error: Module name required\n");
        return -1;
    }

    char normalized_name[APM_MAX_NAME_LEN];
    if (apm_normalize_module_name(module_name, normalized_name, sizeof(normalized_name)) < 0) {
        vga_puts("[APM] Error: Invalid module name\n");
        return -1;
    }

    char module_path[256];
    char resolved_name[MODULE_NAME_LEN];
    resolved_name[0] = '\0';
    if (apm_build_module_path(module_name, module_path, sizeof(module_path)) == 0) {
        (void)apm_get_module_identity(module_path, resolved_name, sizeof(resolved_name), NULL);
    }

    char candidates[3][APM_MAX_NAME_LEN];
    int candidate_count = 0;

    if (resolved_name[0] != '\0') {
        strncpy(candidates[candidate_count], resolved_name, APM_MAX_NAME_LEN - 1);
        candidates[candidate_count][APM_MAX_NAME_LEN - 1] = '\0';
        candidate_count++;
    }

    bool seen = false;
    for (int i = 0; i < candidate_count; i++) {
        if (strcmp(candidates[i], normalized_name) == 0) {
            seen = true;
            break;
        }
    }
    if (!seen && candidate_count < 3) {
        strncpy(candidates[candidate_count], normalized_name, APM_MAX_NAME_LEN - 1);
        candidates[candidate_count][APM_MAX_NAME_LEN - 1] = '\0';
        candidate_count++;
    }

    const char* base = apm_basename(module_name);
    if (base && base[0] != '\0') {
        seen = false;
        for (int i = 0; i < candidate_count; i++) {
            if (strcmp(candidates[i], base) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen && candidate_count < 3) {
            strncpy(candidates[candidate_count], base, APM_MAX_NAME_LEN - 1);
            candidates[candidate_count][APM_MAX_NAME_LEN - 1] = '\0';
            candidate_count++;
        }
    }

    for (int i = 0; i < candidate_count; i++) {
        if (kmodule_unload_v2(candidates[i]) == 0 || kmodule_unload(candidates[i]) == 0) {
            vga_puts("[APM] Unloaded module: ");
            vga_puts(candidates[i]);
            vga_puts("\n");
            return 0;
        }
    }

    vga_puts("[APM] Error: Module is not loaded: ");
    vga_puts(module_name);
    vga_puts("\n");
    return -1;
}

int apm_set_module_autoload(const char* module_name, bool enabled) {
    if (!module_name || module_name[0] == '\0') {
        return -1;
    }

    char normalized_name[APM_MAX_NAME_LEN];
    if (apm_normalize_module_name(module_name, normalized_name, sizeof(normalized_name)) < 0) {
        return -1;
    }

    if (enabled) {
        char module_path[256];
        if (apm_build_module_path(module_name, module_path, sizeof(module_path)) < 0) {
            vga_puts("[APM] Error: Invalid module path for autoload\n");
            return -1;
        }
        int fd_check = vfs_open(module_path, O_RDONLY);
        if (fd_check < 0) {
            vga_puts("[APM] Error: Module is not installed: ");
            vga_puts(normalized_name);
            vga_puts("\n");
            return -1;
        }
        vfs_close(fd_check);
    }

    char entries[APM_MAX_MODULES][APM_MAX_NAME_LEN];
    int count = apm_read_autoload_entries(entries, APM_MAX_MODULES);
    if (count < 0) {
        vga_puts("[APM] Error: Failed to read autoload configuration\n");
        return -1;
    }

    int found_index = -1;
    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i], normalized_name) == 0) {
            found_index = i;
            break;
        }
    }

    if (enabled) {
        if (found_index >= 0) {
            vga_puts("[APM] Autoload already enabled for ");
            vga_puts(normalized_name);
            vga_puts("\n");
            return 0;
        }
        if (count >= APM_MAX_MODULES) {
            vga_puts("[APM] Error: Autoload list is full\n");
            return -1;
        }
        strncpy(entries[count], normalized_name, APM_MAX_NAME_LEN - 1);
        entries[count][APM_MAX_NAME_LEN - 1] = '\0';
        count++;
    } else {
        if (found_index < 0) {
            return 0;
        }
        for (int i = found_index; i < count - 1; i++) {
            strncpy(entries[i], entries[i + 1], APM_MAX_NAME_LEN - 1);
            entries[i][APM_MAX_NAME_LEN - 1] = '\0';
        }
        entries[count - 1][0] = '\0';
        count--;
    }

    if (apm_write_autoload_entries(entries, count) < 0) {
        vga_puts("[APM] Error: Failed to update autoload configuration\n");
        return -1;
    }

    vga_puts("[APM] Autoload ");
    vga_puts(enabled ? "enabled for " : "disabled for ");
    vga_puts(normalized_name);
    vga_puts("\n");
    return 0;
}

int apm_list_autoload_modules(void) {
    char entries[APM_MAX_MODULES][APM_MAX_NAME_LEN];
    int count = apm_read_autoload_entries(entries, APM_MAX_MODULES);
    if (count < 0) {
        vga_puts("[APM] Error: Failed to read autoload list\n");
        return -1;
    }

    vga_puts("\nStartup Auto-load Modules:\n");
    vga_puts("===========================\n");
    if (count == 0) {
        vga_puts("  (none)\n");
        return 0;
    }

    for (int i = 0; i < count; i++) {
        vga_puts("  * ");
        vga_puts(entries[i]);
        vga_puts("\n");
    }

    return 0;
}

int apm_load_startup_modules(void) {
    char entries[APM_MAX_MODULES][APM_MAX_NAME_LEN];
    int count = apm_read_autoload_entries(entries, APM_MAX_MODULES);
    if (count <= 0) {
        serial_puts("[APM] No startup modules configured\n");
        return 0;
    }

    serial_puts("[APM] Loading startup modules...\n");
    int loaded = 0;
    int failed = 0;

    for (int i = 0; i < count; i++) {
        if (apm_load_module_internal(entries[i], false) == 0) {
            loaded++;
        } else {
            failed++;
        }
    }

    char num[16];
    serial_puts("[APM] Startup modules loaded: ");
    itoa(loaded, num, 10);
    serial_puts(num);
    serial_puts(", failed: ");
    itoa(failed, num, 10);
    serial_puts(num);
    serial_puts("\n");

    return failed == 0 ? 0 : -1;
}

int apm_remove_module(const char* module_name) {
    char module_path[256];
    if (apm_build_module_path(module_name, module_path, sizeof(module_path)) < 0) {
        vga_puts("[APM] Error: Invalid module name/path\n");
        return -1;
    }

    int fd = vfs_open(module_path, O_RDONLY);
    if (fd < 0) {
        vga_puts("[APM] Error: Module '");
        vga_puts(module_name);
        vga_puts("' is not installed\n");
        return -1;
    }
    vfs_close(fd);

    // Try to unload if loaded; ignore failures.
    char normalized_name[APM_MAX_NAME_LEN];
    char resolved_name[MODULE_NAME_LEN];
    normalized_name[0] = '\0';
    resolved_name[0] = '\0';
    if (apm_normalize_module_name(module_name, normalized_name, sizeof(normalized_name)) == 0) {
        (void)apm_get_module_identity(module_path, resolved_name, sizeof(resolved_name), NULL);
        if (resolved_name[0] != '\0') {
            (void)kmodule_unload_v2(resolved_name);
            (void)kmodule_unload(resolved_name);
        }
        (void)kmodule_unload_v2(normalized_name);
        (void)kmodule_unload(normalized_name);
    }

    // Remove from startup autoload config as part of deletion.
    apm_set_module_autoload(module_name, false);

    if (vfs_unlink(module_path) < 0) {
        vga_puts("[APM] Error: Failed to remove module file\n");
        return -1;
    }

    vga_puts("[APM] Module '");
    vga_puts(module_name);
    vga_puts("' removed successfully\n");
    return 0;
}
