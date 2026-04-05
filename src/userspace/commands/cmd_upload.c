/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_upload.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <command_registry.h>
#include <net/http.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <umalloc.h>
#include <vga.h>
#include <serial.h>

#define TEMPDB_URL "http://tempdb.aosproject.workers.dev/u"
#define MAX_UPLOAD_SIZE (10 * 1024 * 1024)  // 10 MiB
#define DEFAULT_TTL 300
#define MAX_TTL 600
#define O_RDONLY 0x0000  // Read-only
#define FILE_READ_CHUNK 4096  // Read file in 4KB chunks

extern void kprint(const char *str);

/**
 * Read file contents into memory using syscalls
 * Returns allocated buffer or NULL on error
 */
static uint8_t* read_file_contents(const char* path, uint32_t* out_size) {
    if (!path || !out_size) {
        return NULL;
    }

    // Open file for reading
    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        char buf[256];
        strcpy(buf, "Error: Cannot open file '");
        strcat(buf, path);
        strcat(buf, "'");
        kprint(buf);
        return NULL;
    }

    // First, allocate a reasonable buffer size
    // We'll read the file in chunks if it's larger
    uint32_t buffer_size = FILE_READ_CHUNK;
    uint8_t* buffer = (uint8_t*)umalloc(buffer_size);
    if (!buffer) {
        kprint("Error: Out of memory");
        sys_close(fd);
        return NULL;
    }

    // Read file contents
    uint32_t total_read = 0;
    int bytes_read;
    
    while ((bytes_read = sys_read(fd, buffer + total_read, buffer_size - total_read)) > 0) {
        total_read += bytes_read;

        // Check if we need to allocate more space
        if (total_read < FILE_READ_CHUNK) {
            // Still have space in current buffer
            break;
        }

        // Need to expand buffer
        if (total_read >= MAX_UPLOAD_SIZE) {
            char buf[256];
            strcpy(buf, "Error: File exceeds 10 MiB limit");
            kprint(buf);
            sys_close(fd);
            ufree(buffer);
            return NULL;
        }

        // Expand buffer
        uint32_t new_size = buffer_size + FILE_READ_CHUNK;
        if (new_size > MAX_UPLOAD_SIZE) {
            new_size = MAX_UPLOAD_SIZE;
        }
        
        uint8_t* new_buffer = (uint8_t*)urealloc(buffer, new_size);
        if (!new_buffer) {
            kprint("Error: Out of memory while expanding buffer");
            sys_close(fd);
            ufree(buffer);
            return NULL;
        }
        
        buffer = new_buffer;
        buffer_size = new_size;
    }

    sys_close(fd);

    if (total_read == 0) {
        kprint("Error: File is empty");
        ufree(buffer);
        return NULL;
    }

    if (bytes_read < 0) {
        kprint("Error: Failed to read file");
        ufree(buffer);
        return NULL;
    }

    *out_size = total_read;
    return buffer;
}

/**
 * Extract filename from path (last component)
 */
static void get_filename_from_path(const char* path, char* filename, size_t max_len) {
    if (!path || !filename) {
        return;
    }

    const char* last_slash = strrchr(path, '/');
    const char* name = last_slash ? last_slash + 1 : path;

    if (name[0] == '\0') {
        strcpy(filename, "upload");
    } else {
        strncpy(filename, name, max_len - 1);
        filename[max_len - 1] = '\0';
    }
}

/**
 * Build upload URL with query parameters
 */
static void build_upload_url(const char* filename, uint32_t ttl, char* url) {
    strcpy(url, TEMPDB_URL);
    strcat(url, "?ttl=");
    
    char ttl_str[16];
    itoa(ttl, ttl_str, 10);
    strcat(url, ttl_str);
    
    if (filename && filename[0] != '\0') {
        strcat(url, "&filename=");
        strcat(url, filename);
    }
}

/**
 * Extract download URL from HTTP response body
 */
static int extract_download_url(const uint8_t* body, uint32_t body_len, char* url, size_t max_len) {
    if (!body || body_len == 0) {
        return -1;
    }

    // The response is a plain-text URL, just copy it
    // Make sure it's null-terminated
    uint32_t copy_len = body_len < (max_len - 1) ? body_len : (max_len - 1);
    memcpy(url, body, copy_len);
    url[copy_len] = '\0';

    // Trim whitespace and newlines
    while (copy_len > 0 && (url[copy_len - 1] == '\n' || url[copy_len - 1] == '\r' || url[copy_len - 1] == ' ')) {
        copy_len--;
        url[copy_len] = '\0';
    }

    return (url[0] != '\0') ? 0 : -1;
}

/**
 * Main upload command handler
 */
static void cmd_upload(const char* args) {
    if (!args || args[0] == '\0') {
        kprint("Usage: upload <file> [ttl_seconds]");
        kprint("  <file>: Path to file to upload");
        kprint("  [ttl_seconds]: TTL in seconds (default 300, max 600)");
        kprint("Example: upload /tmp/data.bin 300");
        return;
    }

    // Parse arguments
    char filepath[256] = {0};
    uint32_t ttl = DEFAULT_TTL;

    // Extract filepath (first argument)
    const char* space = strchr(args, ' ');
    if (space) {
        size_t len = space - args;
        if (len >= sizeof(filepath)) {
            len = sizeof(filepath) - 1;
        }
        strncpy(filepath, args, len);
        filepath[len] = '\0';

        // Parse TTL if provided
        const char* ttl_str = space + 1;
        while (*ttl_str == ' ') ttl_str++;  // Skip whitespace
        if (*ttl_str != '\0') {
            ttl = atoi(ttl_str);
            if (ttl > MAX_TTL) {
                ttl = MAX_TTL;
            }
            if (ttl == 0) {
                ttl = DEFAULT_TTL;
            }
        }
    } else {
        strncpy(filepath, args, sizeof(filepath) - 1);
        filepath[sizeof(filepath) - 1] = '\0';
    }

    // Trim trailing whitespace from filepath
    for (int i = strlen(filepath) - 1; i >= 0 && filepath[i] == ' '; i--) {
        filepath[i] = '\0';
    }

    if (strlen(filepath) == 0) {
        kprint("Error: Invalid filepath");
        return;
    }

    vga_puts("Uploading: ");
    vga_puts(filepath);
    vga_puts("\n");

    // Read file contents
    uint32_t file_size = 0;
    uint8_t* file_data = read_file_contents(filepath, &file_size);
    if (!file_data) {
        return;
    }

    vga_puts("File size: ");
    char size_buf[32];
    itoa(file_size, size_buf, 10);
    vga_puts(size_buf);
    vga_puts(" bytes\n");

    // Prepare upload request
    char filename[128] = {0};
    get_filename_from_path(filepath, filename, sizeof(filename));

    char upload_url[512] = {0};
    build_upload_url(filename, ttl, upload_url);

    vga_puts("Connecting to tempdb service...\n");

    // Create HTTP request
    http_request_t* request = http_request_create(HTTP_METHOD_PUT, upload_url);
    if (!request) {
        kprint("Error: Failed to create HTTP request");
        ufree(file_data);
        return;
    }

    // Set request body
    if (http_request_set_body(request, file_data, file_size) != 0) {
        kprint("Error: Failed to set request body");
        http_request_free(request);
        ufree(file_data);
        return;
    }

    // Add Content-Type header
    http_request_add_header(request, "Content-Type", "application/octet-stream");

    // Send request
    http_response_t* response = http_response_create();
    if (!response) {
        kprint("Error: Failed to create HTTP response");
        http_request_free(request);
        ufree(file_data);
        return;
    }

    int result = http_send(request, response);

    ufree(file_data);
    http_request_free(request);

    if (result != 0) {
        kprint("Error: HTTP request failed");
        http_response_free(response);
        return;
    }

    // Check response status
    if (response->status_code != HTTP_STATUS_OK && response->status_code != HTTP_STATUS_CREATED) {
        char status_buf[128];
        strcpy(status_buf, "Error: HTTP ");
        char code_str[16];
        itoa(response->status_code, code_str, 10);
        strcat(status_buf, code_str);
        strcat(status_buf, " ");
        strcat(status_buf, http_status_text(response->status_code));
        kprint(status_buf);
        http_response_free(response);
        return;
    }

    // Extract download URL from response
    char download_url[512] = {0};
    if (extract_download_url(response->body, response->body_len, download_url, sizeof(download_url)) != 0) {
        kprint("Error: Failed to parse response URL");
        http_response_free(response);
        return;
    }

    http_response_free(response);

    // Display success message with URL
    vga_puts("\nFile uploaded successfully!\n");
    vga_puts("Download URL: ");
    vga_puts(download_url);
    vga_puts("\n");
}

void cmd_module_upload_register(void) {
    command_register_with_category(
        "upload",
        "upload <file> [ttl_seconds]",
        "Upload file to aos tempdb service and return download URL",
        "Network",
        cmd_upload
    );
}
