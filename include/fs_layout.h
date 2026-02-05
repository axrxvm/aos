/*
 * === AOS HEADER BEGIN ===
 * ./include/fs_layout.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef FS_LAYOUT_H
#define FS_LAYOUT_H

#include <stdint.h>

// Standard directory paths
#define FS_ROOT_DIR       "/"
#define FS_SYS_DIR        "/sys"
#define FS_USR_DIR        "/usr"
#define FS_BIN_DIR        "/bin"
#define FS_TMP_DIR        "/tmp"
#define FS_DEV_DIR        "/dev"
#define FS_PROC_DIR       "/proc"
#define FS_ETC_DIR        "/etc"

// System subdirectories
#define FS_SYS_CONFIG_DIR "/sys/config"
#define FS_SYS_LOG_DIR    "/sys/log"
#define FS_SYS_DATA_DIR   "/sys/data"

// User database file
#define USER_DATABASE_PATH "/sys/config/users.db"

// Filesystem mode flags
#define FS_MODE_ISO   0  // Running on ramfs (ISO mode)
#define FS_MODE_LOCAL 1  // Running on disk filesystem (SimpleFS or FAT32)

/**
 * Initialize filesystem layout
 * Creates standard directory structure
 * @param mode FS_MODE_ISO or FS_MODE_LOCAL
 * @return 0 on success, negative on error
 */
int fs_layout_init(int mode);

/**
 * Create user home directory
 * @param username Username
 * @return 0 on success, negative on error
 */
int fs_layout_create_user_home(const char* username);

/**
 * Get current filesystem mode
 * @return FS_MODE_ISO or FS_MODE_LOCAL
 */
int fs_layout_get_mode(void);

/**
 * Get home directory path for user
 * @param username Username
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, negative on error
 */
int fs_layout_get_user_home(const char* username, char* buffer, uint32_t size);

/**
 * Expand tilde (~) in path to user's home directory
 * @param path Input path (may contain ~)
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, negative on error
 */
int fs_layout_expand_tilde(const char* path, char* buffer, uint32_t size);

#endif // FS_LAYOUT_H
