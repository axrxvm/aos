/*
 * === AOS HEADER BEGIN ===
 * include/user.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef USER_H
#define USER_H

#include <stdint.h>
#include <stddef.h>

#define MAX_USERS 32
#define MAX_USERNAME 32
#define MAX_PASSWORD_HASH 65  // SHA-256 hex string (64 chars) + null terminator
#define MAX_HOME_DIR 128
#define MAX_SHELL 64

// User ID definitions
#define UID_ROOT 0
#define UID_SYSTEM 1
#define UID_USER_START 1000

// Group ID definitions
#define GID_ROOT 0
#define GID_WHEEL 1
#define GID_USERS 100

// User structure
typedef struct {
    uint32_t uid;                      // User ID
    uint32_t gid;                      // Primary group ID
    char username[MAX_USERNAME];       // Username
    char password_hash[MAX_PASSWORD_HASH]; // SHA-256 hash of password
    char home_dir[MAX_HOME_DIR];       // Home directory path
    char shell[MAX_SHELL];             // Shell path
    uint32_t flags;                    // User flags
} user_t;

// User flags
#define USER_FLAG_ACTIVE           0x01  // User account is active
#define USER_FLAG_LOCKED           0x02  // User account is locked
#define USER_FLAG_ADMIN            0x04  // User has administrative privileges
#define USER_FLAG_NOLOGIN          0x08  // User cannot login (system user)
#define USER_FLAG_MUST_CHANGE_PASS 0x10  // User must change password on next login

// Current session information
typedef struct {
    user_t* user;                      // Current logged-in user
    uint32_t login_time;               // Login timestamp
    uint32_t session_flags;            // Session flags
} session_t;

// Session flags
#define SESSION_FLAG_LOGGED_IN  0x01
#define SESSION_FLAG_ROOT       0x02

/**
 * Initialize user management system
 */
void user_init(void);

/**
 * Create a new user
 * @param username Username
 * @param password Plain-text password (will be hashed)
 * @param uid User ID (0 for auto-assign)
 * @param gid Group ID
 * @param home_dir Home directory path
 * @param shell Shell path
 * @return 0 on success, negative on error
 */
int user_create(const char* username, const char* password, uint32_t uid, uint32_t gid,
                const char* home_dir, const char* shell);

/**
 * Delete a user
 * @param username Username to delete
 * @return 0 on success, negative on error
 */
int user_delete(const char* username);

/**
 * Find user by username
 * @param username Username to find
 * @return Pointer to user structure, NULL if not found
 */
user_t* user_find_by_name(const char* username);

/**
 * Find user by UID
 * @param uid User ID to find
 * @return Pointer to user structure, NULL if not found
 */
user_t* user_find_by_uid(uint32_t uid);

/**
 * Authenticate user with password
 * @param username Username
 * @param password Plain-text password to verify
 * @return Pointer to user structure on success, NULL on failure
 */
user_t* user_authenticate(const char* username, const char* password);

/**
 * Change user password
 * @param username Username
 * @param old_password Old password (for verification)
 * @param new_password New password
 * @return 0 on success, negative on error
 */
int user_change_password(const char* username, const char* old_password, const char* new_password);

/**
 * Change user password (privileged operation, no old password verification)
 * @param username Username
 * @param new_password New password
 * @return 0 on success, negative on error
 */
int user_set_password(const char* username, const char* new_password);

/**
 * Get current session
 * @return Pointer to current session
 */
session_t* user_get_session(void);

/**
 * Login user (create session)
 * @param user User to login
 * @return 0 on success, negative on error
 */
int user_login(user_t* user);

/**
 * Logout current user
 */
void user_logout(void);

/**
 * Check if current user is root
 * @return 1 if root, 0 otherwise
 */
int user_is_root(void);

/**
 * Check if current user has admin privileges
 * @return 1 if admin, 0 otherwise
 */
int user_is_admin(void);

/**
 * Save user database to file
 * @param path File path to save to
 * @return 0 on success, negative on error
 */
int user_save_database(const char* path);

/**
 * Load user database from file
 * @param path File path to load from
 * @return 0 on success, negative on error
 */
int user_load_database(const char* path);

/**
 * List all users
 * @param callback Callback function for each user
 * @param user_data User data to pass to callback
 */
void user_list_all(void (*callback)(user_t* user, void* user_data), void* user_data);

/**
 * Get user count
 * @return Number of users in system
 */
uint32_t user_get_count(void);

#endif // USER_H
