/*
 * === AOS HEADER BEGIN ===
 * ./include/fs/vfs.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <fileperm.h>

// File types
#define VFS_FILE      0x01
#define VFS_DIRECTORY 0x02
#define VFS_CHARDEV   0x03
#define VFS_BLOCKDEV  0x04
#define VFS_PIPE      0x05
#define VFS_SYMLINK   0x06
#define VFS_MOUNTPT   0x08

// File open flags
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_TRUNC   0x0200
#define O_APPEND  0x0400
#define O_DIRECTORY 0x10000

// Seek modes
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Error codes
#define VFS_OK           0
#define VFS_ERR_NOTFOUND -1
#define VFS_ERR_NOSPACE  -2
#define VFS_ERR_INVALID  -3
#define VFS_ERR_EXISTS   -4
#define VFS_ERR_NOTDIR   -5
#define VFS_ERR_ISDIR    -6
#define VFS_ERR_NOTEMPTY -7
#define VFS_ERR_PERM     -8
#define VFS_ERR_IO       -9

// Forward declarations
struct vnode;
struct filesystem;
struct mount;
struct file;

// Directory entry structure
typedef struct dirent {
    char name[256];
    uint32_t inode;
    uint8_t type;
} dirent_t;

// File statistics
typedef struct stat {
    uint32_t st_dev;     // Device ID
    uint32_t st_ino;     // Inode number
    uint32_t st_mode;    // File type and mode
    uint32_t st_nlink;   // Number of hard links
    uint32_t st_uid;     // User ID
    uint32_t st_gid;     // Group ID
    uint32_t st_rdev;    // Device ID (if special file)
    uint32_t st_size;    // Total size in bytes
    uint32_t st_blksize; // Block size for filesystem I/O
    uint32_t st_blocks;  // Number of 512B blocks allocated
} stat_t;

// VFS operations for vnodes
typedef struct vnode_ops {
    int (*open)(struct vnode* node, uint32_t flags);
    int (*close)(struct vnode* node);
    int (*read)(struct vnode* node, void* buffer, uint32_t size, uint32_t offset);
    int (*write)(struct vnode* node, const void* buffer, uint32_t size, uint32_t offset);
    struct vnode* (*finddir)(struct vnode* node, const char* name);
    struct vnode* (*create)(struct vnode* parent, const char* name, uint32_t flags);
    int (*unlink)(struct vnode* parent, const char* name);
    int (*mkdir)(struct vnode* parent, const char* name);
    int (*readdir)(struct vnode* node, uint32_t index, dirent_t* dirent);
    int (*stat)(struct vnode* node, stat_t* stat);
} vnode_ops_t;

// Virtual node (inode)
typedef struct vnode {
    char name[256];              // File name
    uint32_t inode;              // Inode number
    uint32_t type;               // File type (VFS_FILE, VFS_DIRECTORY, etc.)
    uint32_t size;               // Size in bytes
    uint32_t flags;              // Flags
    uint32_t refcount;           // Reference count
    file_access_t access;        // File permissions (v0.7.3)
    struct filesystem* fs;       // Filesystem this vnode belongs to
    struct mount* mount;         // Mount point (if this is a mountpoint)
    void* fs_data;               // Filesystem-specific data
    vnode_ops_t* ops;            // Operations
} vnode_t;

// Filesystem operations
typedef struct filesystem_ops {
    int (*mount)(struct filesystem* fs, const char* source, uint32_t flags);
    int (*unmount)(struct filesystem* fs);
    vnode_t* (*get_root)(struct filesystem* fs);
} filesystem_ops_t;

// Filesystem structure
typedef struct filesystem {
    const char* name;            // Filesystem type name (e.g., "ramfs")
    filesystem_ops_t* ops;       // Filesystem operations
    void* fs_data;               // Filesystem-specific private data
    struct mount* mount;         // Associated mount point
} filesystem_t;

// Mount point structure
typedef struct mount {
    char mountpoint[256];        // Mount point path
    vnode_t* vnode;              // Vnode of the mount point
    filesystem_t* fs;            // Filesystem mounted here
    uint32_t flags;              // Mount flags
    struct mount* next;          // Next mount in list
} mount_t;

// File descriptor structure
typedef struct file {
    vnode_t* vnode;              // Vnode this file references
    uint32_t flags;              // Open flags
    uint32_t offset;             // Current file offset
    uint32_t refcount;           // Reference count
} file_t;

// VFS initialization
void vfs_init(void);

// Filesystem registration
int vfs_register_filesystem(filesystem_t* fs);
int vfs_unregister_filesystem(const char* name);

// Mount operations
int vfs_mount(const char* source, const char* target, const char* fstype, uint32_t flags);
int vfs_unmount(const char* target);

// Path resolution
vnode_t* vfs_resolve_path(const char* path);
char* vfs_normalize_path(const char* path);

// File operations
int vfs_open(const char* path, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, void* buffer, uint32_t size);
int vfs_write(int fd, const void* buffer, uint32_t size);
int vfs_lseek(int fd, int offset, int whence);

// Directory operations
int vfs_readdir(int fd, dirent_t* dirent);
int vfs_mkdir(const char* path);
int vfs_rmdir(const char* path);

// File management
int vfs_unlink(const char* path);
int vfs_stat(const char* path, stat_t* stat);

// Working directory operations
const char* vfs_getcwd(void);
int vfs_chdir(const char* path);

// Vnode reference counting
void vfs_vnode_acquire(vnode_t* vnode);
void vfs_vnode_release(vnode_t* vnode);

// File descriptor operations
file_t* vfs_get_file(int fd);

// Mount table access
mount_t* vfs_get_mount(int index);

// Permission operations (v0.7.3)
int vfs_check_access(vnode_t* vnode, uint32_t requester_id, 
                     owner_type_t requester_type, access_check_t check);
int vfs_set_owner(const char* path, uint32_t owner_id, owner_type_t owner_type);
int vfs_set_access(const char* path, uint8_t owner_bits, uint8_t other_bits);
int vfs_get_access(const char* path, file_access_t* access);

#endif // VFS_H
