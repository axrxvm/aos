/*
 * === AOS HEADER BEGIN ===
 * ./include/fileperm.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef FILEPERM_H
#define FILEPERM_H

#include <stdint.h>
#include <stddef.h>

// aOS "Access Bits"

// Access bit flags (similar to rwx but aOS naming)
#define ACCESS_VIEW     (1 << 0)    // Can read/view file
#define ACCESS_MODIFY   (1 << 1)    // Can write/modify file
#define ACCESS_RUN      (1 << 2)    // Can execute file
#define ACCESS_DELETE   (1 << 3)    // Can delete file
#define ACCESS_OWNER    (1 << 4)    // Is owner of file
#define ACCESS_SYSTEM   (1 << 5)    // System file (protected)
#define ACCESS_HIDDEN   (1 << 6)    // Hidden from normal listings
#define ACCESS_LOCK     (1 << 7)    // File is locked (immutable)

// Shorthand access combinations
#define ACCESS_NONE     0
#define ACCESS_READ     (ACCESS_VIEW)
#define ACCESS_WRITE    (ACCESS_VIEW | ACCESS_MODIFY)
#define ACCESS_FULL     (ACCESS_VIEW | ACCESS_MODIFY | ACCESS_RUN | ACCESS_DELETE)

// Owner types - aOS hierarchy
typedef enum {
    OWNER_SYSTEM = 0,       // The godfather - kernel-owned files (no user UID)
    OWNER_ROOT = 1,         // Root user (UID 0)
    OWNER_ADMIN = 2,        // Administrator level
    OWNER_PRGMS = 3,        // Program-managed files
    OWNER_USR = 4,          // User-managed files
    OWNER_BASIC = 5         // Basic access - everyone can access
} owner_type_t;

// File access control (per file)
typedef struct {
    uint32_t owner_id;          // Owner identifier
    owner_type_t owner_type;    // Owner type
    uint8_t owner_access;       // Owner's access bits
    uint8_t other_access;       // Everyone else's access bits
    uint32_t flags;             // Additional flags
} file_access_t;

// Check access permissions
typedef enum {
    CHECK_VIEW = 0,
    CHECK_MODIFY = 1,
    CHECK_RUN = 2,
    CHECK_DELETE = 3,
    CHECK_OWN = 4
} access_check_t;

// Initialize file permission system
void fileperm_init(void);

// Permission operations
int fileperm_check(const file_access_t* access, uint32_t requester_id, 
                   owner_type_t requester_type, access_check_t check);
int fileperm_set(const char* path, const file_access_t* access);
int fileperm_get(const char* path, file_access_t* access);
int fileperm_change_owner(const char* path, uint32_t owner_id, owner_type_t owner_type);
int fileperm_change_access(const char* path, uint8_t owner_bits, uint8_t other_bits);

// Access bit manipulation
uint8_t access_combine(uint8_t a, uint8_t b);
uint8_t access_remove(uint8_t a, uint8_t b);
int access_has(uint8_t bits, uint8_t required);

// Default permissions for new files
file_access_t fileperm_default_file(uint32_t owner_id, owner_type_t owner_type);
file_access_t fileperm_default_dir(uint32_t owner_id, owner_type_t owner_type);

// System owner checks
int is_system_owner(uint32_t owner_id, owner_type_t owner_type);
int is_root_owner(uint32_t owner_id, owner_type_t owner_type);

// Permission masks for common operations
extern const uint8_t PERM_FILE_DEFAULT;      // View + Modify for owner
extern const uint8_t PERM_FILE_READONLY;     // View only for owner
extern const uint8_t PERM_FILE_PRIVATE;      // Full for owner, none for others
extern const uint8_t PERM_FILE_PUBLIC;       // View for everyone
extern const uint8_t PERM_DIR_DEFAULT;       // Full for owner, view for others
extern const uint8_t PERM_EXEC_DEFAULT;      // View + Run for owner

#endif // FILEPERM_H
