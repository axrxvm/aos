/*
 * === AOS HEADER BEGIN ===
 * src/system/fs_layout.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <fs_layout.h>
#include <fs/vfs.h>
#include <user.h>
#include <string.h>
#include <serial.h>
#include <stdlib.h>
#include <stdlib.h>

static int current_fs_mode = FS_MODE_ISO;

// Helper function to create directory if it doesn't exist
static int mkdir_if_not_exists(const char* path) {
    vnode_t* node = vfs_resolve_path(path);
    if (node) {
        // Directory already exists
        return 0;
    }
    
    // Create directory
    int ret = vfs_mkdir(path);
    if (ret == VFS_OK) {
        serial_puts("FS Layout: Created directory '");
        serial_puts(path);
        serial_puts("'\n");
        return 0;
    } else {
        serial_puts("FS Layout: Failed to create directory '");
        serial_puts(path);
        serial_puts("'\n");
        return -1;
    }
}

int fs_layout_init(int mode) {
    serial_puts("Initializing filesystem layout (mode: ");
    serial_puts(mode == FS_MODE_LOCAL ? "LOCAL" : "ISO");
    serial_puts(")...\n");
    
    current_fs_mode = mode;
    
    // Create standard directories
    int errors = 0;
    
    // System directories
    errors += mkdir_if_not_exists(FS_SYS_DIR);
    errors += mkdir_if_not_exists(FS_SYS_CONFIG_DIR);
    errors += mkdir_if_not_exists(FS_SYS_LOG_DIR);
    errors += mkdir_if_not_exists(FS_SYS_DATA_DIR);
    
    // User directories
    errors += mkdir_if_not_exists(FS_USR_DIR);
    
    // Other directories
    errors += mkdir_if_not_exists(FS_BIN_DIR);
    errors += mkdir_if_not_exists(FS_TMP_DIR);
    errors += mkdir_if_not_exists(FS_DEV_DIR);
    errors += mkdir_if_not_exists(FS_PROC_DIR);
    errors += mkdir_if_not_exists(FS_ETC_DIR);
    
    // Create root user home directory
    errors += mkdir_if_not_exists("/usr/root");
    errors += mkdir_if_not_exists("/usr/root/home");
    
    if (errors > 0) {
        serial_puts("FS Layout: Initialization completed with ");
        char buf[16];
        itoa(errors, buf, 10);
        serial_puts(buf);
        serial_puts(" errors\n");
        return -1;
    }
    
    serial_puts("FS Layout: Filesystem layout initialized successfully\n");
    return 0;
}

int fs_layout_create_user_home(const char* username) {
    if (!username) {
        return -1;
    }
    
    char path[128];
    
    // Create /usr/<username>
    strncpy(path, "/usr/", sizeof(path) - 1);
    strncat(path, username, sizeof(path) - strlen(path) - 1);
    path[sizeof(path) - 1] = '\0';
    
    if (mkdir_if_not_exists(path) != 0) {
        return -2;
    }
    
    // Create /usr/<username>/home
    strncat(path, "/home", sizeof(path) - strlen(path) - 1);
    path[sizeof(path) - 1] = '\0';
    
    if (mkdir_if_not_exists(path) != 0) {
        return -3;
    }
    
    serial_puts("FS Layout: Created home directory for user '");
    serial_puts(username);
    serial_puts("'\n");
    
    return 0;
}

int fs_layout_get_mode(void) {
    return current_fs_mode;
}

int fs_layout_get_user_home(const char* username, char* buffer, uint32_t size) {
    if (!username || !buffer || size == 0) {
        return -1;
    }
    
    // Format: /usr/<username>/home
    uint32_t len = strlen("/usr/") + strlen(username) + strlen("/home");
    if (len >= size) {
        return -2; // Buffer too small
    }
    
    strncpy(buffer, "/usr/", size - 1);
    strncat(buffer, username, size - strlen(buffer) - 1);
    strncat(buffer, "/home", size - strlen(buffer) - 1);
    buffer[size - 1] = '\0';
    
    return 0;
}

int fs_layout_expand_tilde(const char* path, char* buffer, uint32_t size) {
    if (!path || !buffer || size == 0) {
        return -1;
    }
    
    // If path doesn't start with ~, just copy it
    if (path[0] != '~') {
        strncpy(buffer, path, size - 1);
        buffer[size - 1] = '\0';
        return 0;
    }
    
    // Get current user's home directory
    session_t* session = user_get_session();
    if (!session || !session->user) {
        // No logged-in user, cannot expand ~
        return -2;
    }
    
    // Replace ~ with home directory
    const char* home_dir = session->user->home_dir;
    uint32_t home_len = strlen(home_dir);
    uint32_t path_len = strlen(path);
    
    if (path[1] == '\0') {
        // Just "~"
        if (home_len >= size) {
            return -3; // Buffer too small
        }
        strncpy(buffer, home_dir, size - 1);
        buffer[size - 1] = '\0';
    } else if (path[1] == '/') {
        // "~/something"
        if (home_len + path_len - 1 >= size) {
            return -3; // Buffer too small
        }
        strncpy(buffer, home_dir, size - 1);
        strncat(buffer, path + 1, size - strlen(buffer) - 1);
        buffer[size - 1] = '\0';
    } else {
        // Invalid format
        return -4;
    }
    
    return 0;
}
