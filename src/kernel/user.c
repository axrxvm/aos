/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/user.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <user.h>
#include <crypto/sha256.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <fs/vfs.h>
#include <process.h>
#include <fileperm.h>

// User database (static allocation for early boot)
static user_t user_database[MAX_USERS];
static uint32_t user_count = 0;

// Current session
static session_t current_session;

// Helper function: hash password
static void hash_password(const char* password, char* hash_out) {
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_hash((const uint8_t*)password, strlen(password), digest);
    sha256_to_hex(digest, hash_out);
}

// Helper function: get next available UID
static uint32_t get_next_uid(void) {
    uint32_t max_uid = UID_USER_START - 1;
    for (uint32_t i = 0; i < user_count; i++) {
        if (user_database[i].uid > max_uid) {
            max_uid = user_database[i].uid;
        }
    }
    return max_uid + 1;
}

void user_init(void) {
    serial_puts("Initializing user management system...\n");
    
    // Clear user database
    memset(user_database, 0, sizeof(user_database));
    user_count = 0;
    
    // Clear session
    memset(&current_session, 0, sizeof(session_t));
    
    // Create root user with temporary password "root"
    // IMPORTANT: Use UID_ROOT (0) explicitly, not 0 which means auto-assign
    // This will be changed on first login
    user_create("root", "root", UID_ROOT, GID_ROOT, "/usr/root/home", "/bin/shell");
    
    // Set root user as admin
    user_t* root = user_find_by_uid(UID_ROOT);
    if (root) {
        root->flags |= USER_FLAG_ADMIN;
        serial_puts("Root user created with admin privileges\n");
    }
    
    serial_puts("User management system initialized.\n");
}

int user_create(const char* username, const char* password, uint32_t uid, uint32_t gid,
                const char* home_dir, const char* shell) {
    if (!username || !password || !home_dir || !shell) {
        return -1;
    }
    
    // Check if user already exists
    if (user_find_by_name(username) != NULL) {
        serial_puts("User: User '");
        serial_puts(username);
        serial_puts("' already exists\n");
        return -2;
    }
    
    // Check if database is full
    if (user_count >= MAX_USERS) {
        serial_puts("User: Database full\n");
        return -3;
    }
    
    // Auto-assign UID only if not specified (and not 0 which is UID_ROOT)
    // Exception: Allow explicit UID 0 for root user
    if (uid == 0 && strcmp(username, "root") != 0) {
        uid = get_next_uid();
    }
    
    // Check if UID is already in use
    if (user_find_by_uid(uid) != NULL) {
        serial_puts("User: UID already in use\n");
        return -4;
    }
    
    // Create user
    user_t* user = &user_database[user_count];
    user->uid = uid;
    user->gid = gid;
    strncpy(user->username, username, MAX_USERNAME - 1);
    user->username[MAX_USERNAME - 1] = '\0';
    
    // Hash password
    hash_password(password, user->password_hash);
    
    strncpy(user->home_dir, home_dir, MAX_HOME_DIR - 1);
    user->home_dir[MAX_HOME_DIR - 1] = '\0';
    
    strncpy(user->shell, shell, MAX_SHELL - 1);
    user->shell[MAX_SHELL - 1] = '\0';
    
    user->flags = USER_FLAG_ACTIVE;
    
    user_count++;
    
    serial_puts("User: Created user '");
    serial_puts(username);
    serial_puts("' (UID: ");
    char buf[16];
    itoa(uid, buf, 10);
    serial_puts(buf);
    serial_puts(", GID: ");
    itoa(gid, buf, 10);
    serial_puts(buf);
    serial_puts(")\n");
    
    return 0;
}

int user_delete(const char* username) {
    if (!username) {
        return -1;
    }
    
    // Find user
    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_database[i].username, username) == 0) {
            // Don't allow deletion of root user
            if (user_database[i].uid == UID_ROOT) {
                serial_puts("User: Cannot delete root user\n");
                return -2;
            }
            
            // Shift remaining users
            for (uint32_t j = i; j < user_count - 1; j++) {
                user_database[j] = user_database[j + 1];
            }
            
            // Clear last entry
            memset(&user_database[user_count - 1], 0, sizeof(user_t));
            user_count--;
            
            serial_puts("User: Deleted user '");
            serial_puts(username);
            serial_puts("'\n");
            
            return 0;
        }
    }
    
    return -3; // User not found
}

user_t* user_find_by_name(const char* username) {
    if (!username) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < user_count; i++) {
        if (strcmp(user_database[i].username, username) == 0) {
            return &user_database[i];
        }
    }
    
    return NULL;
}

user_t* user_find_by_uid(uint32_t uid) {
    for (uint32_t i = 0; i < user_count; i++) {
        if (user_database[i].uid == uid) {
            return &user_database[i];
        }
    }
    
    return NULL;
}

user_t* user_authenticate(const char* username, const char* password) {
    if (!username || !password) {
        return NULL;
    }
    
    user_t* user = user_find_by_name(username);
    if (!user) {
        serial_puts("User: User '");
        serial_puts(username);
        serial_puts("' not found\n");
        return NULL;
    }
    
    // Check if account is active
    if (!(user->flags & USER_FLAG_ACTIVE)) {
        serial_puts("User: Account '");
        serial_puts(username);
        serial_puts("' is not active\n");
        return NULL;
    }
    
    // Check if account is locked
    if (user->flags & USER_FLAG_LOCKED) {
        serial_puts("User: Account '");
        serial_puts(username);
        serial_puts("' is locked\n");
        return NULL;
    }
    
    // Check if user can login
    if (user->flags & USER_FLAG_NOLOGIN) {
        serial_puts("User: Account '");
        serial_puts(username);
        serial_puts("' cannot login\n");
        return NULL;
    }
    
    // Verify password
    char password_hash[MAX_PASSWORD_HASH];
    hash_password(password, password_hash);
    
    if (strcmp(user->password_hash, password_hash) != 0) {
        serial_puts("User: Invalid password for '");
        serial_puts(username);
        serial_puts("'\n");
        return NULL;
    }
    
    return user;
}

int user_change_password(const char* username, const char* old_password, const char* new_password) {
    if (!username || !old_password || !new_password) {
        return -1;
    }
    
    // Authenticate with old password
    user_t* user = user_authenticate(username, old_password);
    if (!user) {
        return -2;
    }
    
    // Set new password
    hash_password(new_password, user->password_hash);
    
    serial_puts("User: Changed password for '");
    serial_puts(username);
    serial_puts("'\n");
    
    return 0;
}

int user_set_password(const char* username, const char* new_password) {
    if (!username || !new_password) {
        return -1;
    }
    
    user_t* user = user_find_by_name(username);
    if (!user) {
        return -2;
    }
    
    // Set new password
    hash_password(new_password, user->password_hash);
    
    serial_puts("User: Set password for '");
    serial_puts(username);
    serial_puts("'\n");
    
    return 0;
}

session_t* user_get_session(void) {
    return &current_session;
}

int user_login(user_t* user) {
    if (!user) {
        return -1;
    }
    
    current_session.user = user;
    current_session.login_time = 0; // TODO: Use RTC when available
    current_session.session_flags = SESSION_FLAG_LOGGED_IN;
    
    if (user->uid == UID_ROOT) {
        current_session.session_flags |= SESSION_FLAG_ROOT;
    }
    
    // Update current process owner type based on user (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        proc->owner_id = user->uid;
        
        // Set owner type based on user privileges
        if (user->uid == UID_ROOT) {
            proc->owner_type = OWNER_ROOT;
        } else if (user->flags & USER_FLAG_ADMIN) {
            proc->owner_type = OWNER_ADMIN;
        } else {
            proc->owner_type = OWNER_USR;
        }
        
        serial_puts("User: Process owner updated to ");
        switch (proc->owner_type) {
            case OWNER_ROOT: serial_puts("ROOT"); break;
            case OWNER_ADMIN: serial_puts("ADMIN"); break;
            case OWNER_USR: serial_puts("USR"); break;
            default: serial_puts("UNKNOWN"); break;
        }
        serial_puts("\n");
    }
    
    // Change to user's home directory
    if (vfs_chdir(user->home_dir) != 0) {
        serial_puts("User: Failed to change to home directory '");
        serial_puts(user->home_dir);
        serial_puts("', using root\n");
        vfs_chdir("/");
    }
    
    serial_puts("User: Logged in as '");
    serial_puts(user->username);
    serial_puts("'\n");
    
    return 0;
}

void user_logout(void) {
    if (current_session.session_flags & SESSION_FLAG_LOGGED_IN) {
        serial_puts("User: Logging out '");
        serial_puts(current_session.user->username);
        serial_puts("'\n");
        
        // Reset process owner to system (v0.7.3)
        process_t* proc = process_get_current();
        if (proc) {
            proc->owner_type = OWNER_SYSTEM;
            proc->owner_id = 0;
        }
        
        memset(&current_session, 0, sizeof(session_t));
    }
}

int user_is_root(void) {
    return (current_session.session_flags & SESSION_FLAG_ROOT) != 0;
}

int user_is_admin(void) {
    if (!current_session.user) {
        return 0;
    }
    return (current_session.user->flags & USER_FLAG_ADMIN) != 0;
}

int user_save_database(const char* path) {
    if (!path) {
        return -1;
    }
    
    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        serial_puts("User: Failed to open '");
        serial_puts(path);
        serial_puts("' for writing\n");
        return -2;
    }
    
    // Write user count
    vfs_write(fd, &user_count, sizeof(uint32_t));
    
    // Write all users
    for (uint32_t i = 0; i < user_count; i++) {
        vfs_write(fd, &user_database[i], sizeof(user_t));
    }
    
    vfs_close(fd);
    
    serial_puts("User: Saved database to '");
    serial_puts(path);
    serial_puts("'\n");
    
    return 0;
}

int user_load_database(const char* path) {
    if (!path) {
        return -1;
    }
    
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_puts("User: Failed to open '");
        serial_puts(path);
        serial_puts("' for reading\n");
        return -2;
    }
    
    // Read user count
    uint32_t count = 0;
    int bytes_read = vfs_read(fd, &count, sizeof(uint32_t));
    if (bytes_read != sizeof(uint32_t)) {
        serial_puts("User: Failed to read user count\n");
        vfs_close(fd);
        return -3;
    }
    
    if (count > MAX_USERS) {
        serial_puts("User: Invalid user count in database\n");
        vfs_close(fd);
        return -4;
    }
    
    // Read all users
    for (uint32_t i = 0; i < count; i++) {
        bytes_read = vfs_read(fd, &user_database[i], sizeof(user_t));
        if (bytes_read != sizeof(user_t)) {
            serial_puts("User: Failed to read user data\n");
            vfs_close(fd);
            return -5;
        }
    }
    
    user_count = count;
    vfs_close(fd);
    
    serial_puts("User: Loaded database from '");
    serial_puts(path);
    serial_puts("' (");
    char buf[16];
    itoa(user_count, buf, 10);
    serial_puts(buf);
    serial_puts(" users)\n");
    
    return 0;
}

void user_list_all(void (*callback)(user_t* user, void* user_data), void* user_data) {
    if (!callback) {
        return;
    }
    
    for (uint32_t i = 0; i < user_count; i++) {
        callback(&user_database[i], user_data);
    }
}

uint32_t user_get_count(void) {
    return user_count;
}
