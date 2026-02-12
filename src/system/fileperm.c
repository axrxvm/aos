/*
 * === AOS HEADER BEGIN ===
 * src/system/fileperm.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <fileperm.h>
#include <string.h>
#include <serial.h>

// Default permission masks
const uint8_t PERM_FILE_DEFAULT = ACCESS_VIEW | ACCESS_MODIFY;
const uint8_t PERM_FILE_READONLY = ACCESS_VIEW;
const uint8_t PERM_FILE_PRIVATE = ACCESS_FULL;
const uint8_t PERM_FILE_PUBLIC = ACCESS_VIEW;
const uint8_t PERM_DIR_DEFAULT = ACCESS_FULL;
const uint8_t PERM_EXEC_DEFAULT = ACCESS_VIEW | ACCESS_RUN;

// Initialize file permission system
void fileperm_init(void) {
    serial_puts("Initializing file permission system...\n");
    serial_puts("File permission system initialized (Access Bits model).\n");
}

// Check if access is allowed
int fileperm_check(const file_access_t* access, uint32_t requester_id, 
                   owner_type_t requester_type, access_check_t check) {
    if (!access) {
        return 0; // No access info = deny
    }

    // System owners can do anything
    if (requester_type == OWNER_SYSTEM) {
        return 1;
    }

    // Root owners can do almost anything (except modify system files)
    if (requester_type == OWNER_ROOT) {
        if (access->flags & ACCESS_SYSTEM) {
            // Root cannot modify system files unless they own them
            if (access->owner_id != requester_id) {
                return check == CHECK_VIEW ? 1 : 0;
            }
        }
        return 1;
    }

    // Admin owners have elevated privileges
    if (requester_type == OWNER_ADMIN) {
        if (access->flags & ACCESS_SYSTEM) {
            return check == CHECK_VIEW ? 1 : 0; // Can only view system files
        }
        // Can modify user and program files
        if (access->owner_type == OWNER_USR || access->owner_type == OWNER_PRGMS) {
            return 1;
        }
    }

    // Check if requester is owner
    int is_owner = (access->owner_id == requester_id && 
                    access->owner_type == requester_type);

    // Get applicable access bits
    uint8_t bits = is_owner ? access->owner_access : access->other_access;

    // Check if file is locked
    if (access->flags & ACCESS_LOCK) {
        // Locked files can only be viewed
        if (check != CHECK_VIEW) {
            return 0;
        }
    }

    // Map check type to access bit
    uint8_t required_bit = 0;
    switch (check) {
        case CHECK_VIEW:
            required_bit = ACCESS_VIEW;
            break;
        case CHECK_MODIFY:
            required_bit = ACCESS_MODIFY;
            break;
        case CHECK_RUN:
            required_bit = ACCESS_RUN;
            break;
        case CHECK_DELETE:
            required_bit = ACCESS_DELETE;
            break;
        case CHECK_OWN:
            return is_owner ? 1 : 0;
        default:
            return 0;
    }

    return (bits & required_bit) != 0;
}

// Set file permissions (requires ownership or system access)
int fileperm_set(const char* path, const file_access_t* access) {
    // This will be called by VFS layer
    // For now, just validate input
    if (!path || !access) {
        return -1;
    }
    return 0; // Success - VFS will store this
}

// Get file permissions
int fileperm_get(const char* path, file_access_t* access) {
    // This will be called by VFS layer
    if (!path || !access) {
        return -1;
    }
    return 0; // VFS will fill in access
}

// Change file owner
int fileperm_change_owner(const char* path, uint32_t owner_id, owner_type_t owner_type) {
    (void)owner_id;
    (void)owner_type;
    if (!path) {
        return -1;
    }
    // Only current owner or system can change ownership
    return 0; // VFS will handle this
}

// Change access bits
int fileperm_change_access(const char* path, uint8_t owner_bits, uint8_t other_bits) {
    (void)owner_bits;
    (void)other_bits;
    if (!path) {
        return -1;
    }
    // Only owner or system can change access bits
    return 0; // VFS will handle this
}

// Combine access bits (union)
uint8_t access_combine(uint8_t a, uint8_t b) {
    return a | b;
}

// Remove access bits
uint8_t access_remove(uint8_t a, uint8_t b) {
    return a & ~b;
}

// Check if access bits include required bits
int access_has(uint8_t bits, uint8_t required) {
    return (bits & required) == required;
}

// Create default permissions for new file
file_access_t fileperm_default_file(uint32_t owner_id, owner_type_t owner_type) {
    file_access_t access;
    access.owner_id = owner_id;
    access.owner_type = owner_type;
    
    // Set permissions based on owner type
    if (owner_type == OWNER_BASIC) {
        // BASIC files: everyone can read/write
        access.owner_access = ACCESS_VIEW | ACCESS_MODIFY | ACCESS_DELETE;
        access.other_access = ACCESS_VIEW | ACCESS_MODIFY;
    } else if (owner_type == OWNER_SYSTEM) {
        // SYSTEM files: owner full, others read-only
        access.owner_access = ACCESS_VIEW | ACCESS_MODIFY | ACCESS_DELETE;
        access.other_access = ACCESS_VIEW;
        access.flags = ACCESS_SYSTEM;
    } else {
        // Normal files: owner full, others none
        access.owner_access = ACCESS_VIEW | ACCESS_MODIFY | ACCESS_DELETE;
        access.other_access = ACCESS_NONE;
    }
    
    access.flags = 0;
    if (owner_type == OWNER_SYSTEM) {
        access.flags = ACCESS_SYSTEM;
    }
    
    return access;
}

// Create default permissions for new directory
file_access_t fileperm_default_dir(uint32_t owner_id, owner_type_t owner_type) {
    file_access_t access;
    access.owner_id = owner_id;
    access.owner_type = owner_type;
    
    // Set permissions based on owner type
    if (owner_type == OWNER_BASIC) {
        // BASIC directories: everyone can access
        access.owner_access = ACCESS_FULL;
        access.other_access = ACCESS_VIEW | ACCESS_MODIFY;
    } else if (owner_type == OWNER_SYSTEM) {
        // SYSTEM directories: owner full, others view
        access.owner_access = ACCESS_FULL;
        access.other_access = ACCESS_VIEW;
        access.flags = ACCESS_SYSTEM;
    } else {
        // Normal directories: owner full, others view
        access.owner_access = ACCESS_FULL;
        access.other_access = ACCESS_VIEW;
    }
    
    access.flags = 0;
    if (owner_type == OWNER_SYSTEM) {
        access.flags = ACCESS_SYSTEM;
    }
    
    return access;
}

// Check if owner is system
int is_system_owner(uint32_t owner_id, owner_type_t owner_type) {
    (void)owner_id;
    return owner_type == OWNER_SYSTEM;
}

// Check if owner is root
int is_root_owner(uint32_t owner_id, owner_type_t owner_type) {
    (void)owner_id;
    return owner_type == OWNER_ROOT;
}
