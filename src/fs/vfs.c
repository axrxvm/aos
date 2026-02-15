/*
 * === AOS HEADER BEGIN ===
 * src/fs/vfs.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <fs/vfs.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <panic.h>
#include <vmm.h>
#include <fileperm.h>
#include <process.h>

// Maximum number of file descriptors
#define MAX_FDS 256
#define MAX_FILESYSTEMS 16
#define MAX_MOUNTS 32

// Registered filesystems
static filesystem_t* registered_fs[MAX_FILESYSTEMS];
static uint32_t fs_count = 0;

// Mount table
static mount_t mount_table[MAX_MOUNTS];
static uint32_t mount_count = 0;

// File descriptor table
static file_t* fd_table[MAX_FDS];

// Root vnode
static vnode_t* root_vnode = NULL;

// Current working directory (defaults to root)
static vnode_t* cwd = NULL;

// Current working directory path (full path tracking)
static char cwd_path[256] = "/";

void vfs_init(void) {
    serial_puts("Initializing VFS...\n");
    
    // Initialize file descriptor table
    for (int i = 0; i < MAX_FDS; i++) {
        fd_table[i] = NULL;
    }
    
    // Initialize filesystem table
    for (int i = 0; i < MAX_FILESYSTEMS; i++) {
        registered_fs[i] = NULL;
    }
    
    fs_count = 0;
    mount_count = 0;
    root_vnode = NULL;
    cwd = NULL;
    
    // Initialize current working directory path
    cwd_path[0] = '/';
    cwd_path[1] = '\0';
    
    // Initialize mount table
    for (int i = 0; i < MAX_MOUNTS; i++) {
        mount_table[i].fs = NULL;
        mount_table[i].vnode = NULL;
        mount_table[i].next = NULL;
    }
    
    serial_puts("VFS initialized.\n");
}

int vfs_register_filesystem(filesystem_t* fs) {
    if (!fs || !fs->name || !fs->ops) {
        return VFS_ERR_INVALID;
    }
    
    if (fs_count >= MAX_FILESYSTEMS) {
        return VFS_ERR_NOSPACE;
    }
    
    // Check if already registered
    for (uint32_t i = 0; i < fs_count; i++) {
        if (registered_fs[i] && registered_fs[i]->name && strcmp(registered_fs[i]->name, fs->name) == 0) {
            return VFS_ERR_EXISTS;
        }
    }
    
    registered_fs[fs_count++] = fs;
    serial_puts("Registered filesystem: ");
    serial_puts(fs->name);
    serial_puts("\n");
    
    return VFS_OK;
}

int vfs_unregister_filesystem(const char* name) {
    for (uint32_t i = 0; i < fs_count; i++) {
        if (registered_fs[i] && strcmp(registered_fs[i]->name, name) == 0) {
            // Shift remaining entries
            for (uint32_t j = i; j < fs_count - 1; j++) {
                registered_fs[j] = registered_fs[j + 1];
            }
            registered_fs[--fs_count] = NULL;
            return VFS_OK;
        }
    }
    return VFS_ERR_NOTFOUND;
}

static filesystem_t* find_filesystem(const char* name) {
    if (!name) {
        return NULL;
    }
    for (uint32_t i = 0; i < fs_count; i++) {
        if (registered_fs[i] && registered_fs[i]->name && strcmp(registered_fs[i]->name, name) == 0) {
            return registered_fs[i];
        }
    }
    return NULL;
}

static mount_t* find_mount_by_path(const char* path) {
    if (!path) {
        return NULL;
    }

    for (uint32_t i = 0; i < mount_count; i++) {
        mount_t* m = &mount_table[i];
        if (m->fs && strcmp(m->mountpoint, path) == 0) {
            return m;
        }
    }

    return NULL;
}

int vfs_mount(const char* source, const char* target, const char* fstype, uint32_t flags) {
    serial_puts("vfs_mount: entry\n");
    
    if (!target || !fstype) {
        serial_puts("vfs_mount: invalid target or fstype\n");
        return VFS_ERR_INVALID;
    }
    
    serial_puts("vfs_mount: finding filesystem\n");
    // Find filesystem type
    filesystem_t* fs = find_filesystem(fstype);
    if (!fs) {
        serial_puts("vfs_mount: filesystem not found\n");
        return VFS_ERR_NOTFOUND;
    }
    
    serial_puts("vfs_mount: finding free mount slot\n");
    // Find free mount slot
    if (mount_count >= MAX_MOUNTS) {
        serial_puts("vfs_mount: no free mount slots\n");
        return VFS_ERR_NOSPACE;
    }
    
    mount_t* mount = &mount_table[mount_count];
    
    serial_puts("vfs_mount: copying mount point path\n");
    // Copy target path
    uint32_t target_len = strlen(target);
    if (target_len > 255) target_len = 255;
    
    for (uint32_t i = 0; i < target_len; i++) {
        mount->mountpoint[i] = target[i];
    }
    mount->mountpoint[target_len] = '\0';
    
    mount->flags = flags;
    mount->fs = fs;
    mount->next = NULL;
    
    serial_puts("vfs_mount: calling fs mount\n");
    // Call filesystem mount operation
    if (fs->ops->mount) {
        int ret = fs->ops->mount(fs, source, flags);
        if (ret != VFS_OK) {
            serial_puts("vfs_mount: fs mount failed\n");
            mount->fs = NULL;  // Mark as free
            return ret;
        }
    }
    
    serial_puts("vfs_mount: linking fs and mount\n");
    // Link filesystem and mount
    fs->mount = mount;
    
    serial_puts("vfs_mount: getting root vnode\n");
    // Get root vnode from filesystem
    if (fs->ops->get_root) {
        vnode_t* fs_root = fs->ops->get_root(fs);
        mount->vnode = fs_root;
        
        serial_puts("vfs_mount: checking if root mount\n");
        // If mounting at /, set as VFS root
        if (strcmp(target, "/") == 0) {
            serial_puts("vfs_mount: setting as root\n");
            root_vnode = fs_root;
            cwd = fs_root;
            vfs_vnode_acquire(root_vnode);
        }
    }
    
    serial_puts("vfs_mount: incrementing mount count\n");
    // Increment mount count
    mount_count++;
    
    serial_puts("Mounted ");
    serial_puts(fstype);
    serial_puts(" at ");
    serial_puts(target);
    serial_puts("\n");
    
    return VFS_OK;
}

int vfs_unmount(const char* target) {
    for (uint32_t i = 0; i < mount_count; i++) {
        mount_t* mount = &mount_table[i];
        if (mount->fs && strcmp(mount->mountpoint, target) == 0) {
            // Call filesystem unmount
            if (mount->fs->ops->unmount) {
                int ret = mount->fs->ops->unmount(mount->fs);
                if (ret != VFS_OK) {
                    return ret;
                }
            }
            
            // Mark as free
            mount->fs = NULL;
            mount->vnode = NULL;
            
            // Shift remaining mounts
            for (uint32_t j = i; j < mount_count - 1; j++) {
                mount_table[j] = mount_table[j + 1];
            }
            mount_count--;
            return VFS_OK;
        }
    }
    
    return VFS_ERR_NOTFOUND;
}

// Normalize path (resolve . and .., remove duplicate /)
char* vfs_normalize_path(const char* path) {
    if (!path || path[0] == '\0') {
        serial_puts("VFS: normalize_path - null or empty path\n");
        return NULL;
    }
    
    // Check path length is reasonable
    size_t path_len = strlen(path);
    if (path_len > 512) {
        serial_puts("VFS: normalize_path - path too long\n");
        return NULL;
    }
    
    // Allocate buffer for normalized path
    char* result = (char*)kmalloc(256);
    if (!result) {
        return NULL;
    }
    
    memset(result, 0, 256);
    
    // Handle absolute vs relative paths
    int is_absolute = (path[0] == '/');
    
    char* temp = (char*)kmalloc(512);
    if (!temp) {
        kfree(result);
        return NULL;
    }
    
    if (!is_absolute) {
        // Prepend current working directory path
        uint32_t cwd_len = strlen(cwd_path);
        uint32_t path_len = strlen(path);
        
      //  serial_puts("VFS: Normalizing relative path '");
      //  serial_puts(path);
      //  serial_puts("' with cwd='");
      //  serial_puts(cwd_path);
      //  serial_puts("'\n");
        
        if (cwd_len + path_len + 2 > 511) {
            kfree(result);
            kfree(temp);
            return NULL;
        }
        
        strncpy(temp, cwd_path, 511);
        if (cwd_len > 0 && cwd_path[cwd_len - 1] != '/') {
            temp[cwd_len] = '/';
            temp[cwd_len + 1] = '\0';
        }
        strncat(temp, path, 511 - strlen(temp));
        temp[511] = '\0';
    } else {
        strncpy(temp, path, 511);
        temp[511] = '\0';
    }
    
    // Split path into components and resolve . and ..
    char* components[64];
    int component_count = 0;
    
    char* token = temp;
    char* next_slash;
    
    // Skip leading slash
    if (*token == '/') {
        token++;
    }
    
    while (*token) {
        next_slash = strchr(token, '/');
        if (next_slash) {
            *next_slash = '\0';
        }
        
        // Validate component length to prevent overflow
        if (strlen(token) > 255) {
            kfree(result);
            kfree(temp);
            serial_puts("VFS: normalize_path - component too long\n");
            return NULL;
        }
        
        if (strcmp(token, ".") == 0) {
            // Current directory, skip
        } else if (strcmp(token, "..") == 0) {
            // Parent directory, pop last component
            if (component_count > 0) {
                component_count--;
            }
        } else if (*token != '\0') {
            // Regular component
            if (component_count < 64) {
                components[component_count++] = token;
            }
        }
        
        if (next_slash) {
            token = next_slash + 1;
        } else {
            break;
        }
    }
    
    // Build result path
    if (component_count == 0) {
        result[0] = '/';
        result[1] = '\0';
    } else {
        char* dst = result;
        for (int i = 0; i < component_count; i++) {
            *dst++ = '/';
            const char* comp = components[i];
            while (*comp && dst < result + 255) {
                *dst++ = *comp++;
            }
        }
        *dst = '\0';
    }
    
    kfree(temp);
    return result;
}

// Resolve a path to a vnode
vnode_t* vfs_resolve_path(const char* path) {
    if (!path || !root_vnode) {
        return NULL;
    }
    
    // Normalize the path
    char* norm_path = vfs_normalize_path(path);
    if (!norm_path) {
        return NULL;
    }
    
    // If path is root
    if (strcmp(norm_path, "/") == 0) {
        kfree(norm_path);
        return root_vnode;
    }
    
    // Start from root
    vnode_t* current = root_vnode;

    char path_prefix[256];
    path_prefix[0] = '\0';
    
    // Skip leading slash
    char* component = norm_path + 1;
    char* next_slash;
    
    while (*component) {
        if (strlen(path_prefix) + strlen(component) + 2 >= sizeof(path_prefix)) {
            kfree(norm_path);
            return NULL;
        }

        // Find next path component
        next_slash = strchr(component, '/');
        if (next_slash) {
            *next_slash = '\0';
        }

        // Grow prefix and check for mountpoints
        strncat(path_prefix, "/", sizeof(path_prefix) - strlen(path_prefix) - 1);
        strncat(path_prefix, component, sizeof(path_prefix) - strlen(path_prefix) - 1);

        mount_t* mount = find_mount_by_path(path_prefix);
        if (mount && mount->vnode) {
            current = mount->vnode;
        } else {
            // Look up component in current directory
            if (!current->ops || !current->ops->finddir) {
                kfree(norm_path);
                return NULL;
            }
            vnode_t* next = current->ops->finddir(current, component);
            if (!next) {
                kfree(norm_path);
                return NULL;
            }
            current = next;
        }
        
        // Move to next component
        if (next_slash) {
            component = next_slash + 1;
        } else {
            break;
        }
    }
    
    kfree(norm_path);
    return current;
}

// Allocate a file descriptor
static int alloc_fd(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (fd_table[i] == NULL) {
            return i;
        }
    }
    serial_puts("VFS: Out of file descriptors\n");
    return -1;
}

int vfs_open(const char* path, uint32_t flags) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    vnode_t* vnode = vfs_resolve_path(path);
    
    // If file doesn't exist and O_CREAT is set, create it
    if (!vnode && (flags & O_CREAT)) {
        // Extract parent directory and filename
        char* path_copy = vfs_normalize_path(path);
        if (!path_copy) {
            serial_puts("VFS: normalize_path failed\n");
            return VFS_ERR_NOSPACE;
        }
        
        serial_puts("VFS: normalized path for create: ");
        serial_puts(path_copy);
        serial_puts("\n");
        
        char* last_slash = strrchr(path_copy, '/');
        if (!last_slash) {
            serial_puts("VFS: no slash in normalized path\n");
            kfree(path_copy);
            return VFS_ERR_INVALID;
        }
        
        *last_slash = '\0';
        const char* filename = last_slash + 1;
        const char* parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
        
        serial_puts("VFS: parent path: ");
        serial_puts(parent_path);
        serial_puts(", filename: ");
        serial_puts(filename);
        serial_puts("\n");
        
        vnode_t* parent = vfs_resolve_path(parent_path);
        if (!parent) {
            serial_puts("VFS: parent not found\n");
            kfree(path_copy);
            return VFS_ERR_NOTFOUND;
        }
        
        char buf[16];
        serial_puts("VFS: parent vnode type=");
        itoa(parent->type, buf, 10);
        serial_puts(buf);
        serial_puts(" (VFS_DIRECTORY=2)\n");
        
        if (parent->type != VFS_DIRECTORY) {
            serial_puts("VFS: parent is not a directory\n");
            kfree(path_copy);
            return VFS_ERR_NOTDIR;
        }
        
        if (!parent->ops || !parent->ops->create) {
            serial_puts("VFS: parent has no create operation\n");
            kfree(path_copy);
            return VFS_ERR_NOTFOUND;
        }
        
        serial_puts("VFS: calling parent->ops->create\n");
        vnode = parent->ops->create(parent, filename, flags);
        kfree(path_copy);
        
        if (!vnode) {
            serial_puts("VFS: create operation returned NULL\n");
            return VFS_ERR_IO;
        }
    }
    
    if (!vnode) {
        return VFS_ERR_NOTFOUND;
    }
    
    // Check file permissions (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        access_check_t check;
        if (flags & O_WRONLY || flags & O_RDWR) {
            check = CHECK_MODIFY;
        } else {
            check = CHECK_VIEW;
        }
        
        if (!vfs_check_access(vnode, proc->owner_id, proc->owner_type, check)) {
            return VFS_ERR_PERM;
        }
    }
    
    // Check if it's a directory and O_DIRECTORY is not set
    if (vnode->type == VFS_DIRECTORY && !(flags & O_DIRECTORY)) {
        return VFS_ERR_ISDIR;
    }
    
    // Call vnode open operation
    if (vnode->ops && vnode->ops->open) {
        int ret = vnode->ops->open(vnode, flags);
        if (ret != VFS_OK) {
            return ret;
        }
    }
    
    // Allocate file descriptor
    int fd = alloc_fd();
    if (fd < 0) {
        return VFS_ERR_NOSPACE;
    }
    
    // Allocate file structure
    file_t* file = (file_t*)kmalloc(sizeof(file_t));
    if (!file) {
        return VFS_ERR_NOSPACE;
    }
    
    file->vnode = vnode;
    file->flags = flags;
    file->offset = 0;
    file->refcount = 1;
    
    // If O_TRUNC, truncate file to 0
    if (flags & O_TRUNC) {
        vnode->size = 0;
    }
    
    // If O_APPEND, set offset to end
    if (flags & O_APPEND) {
        file->offset = vnode->size;
    }
    
    fd_table[fd] = file;
    vfs_vnode_acquire(vnode);
    
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) {
        return VFS_ERR_INVALID;
    }
    
    file_t* file = fd_table[fd];
    vnode_t* vnode = file->vnode;
    
    // Call vnode close operation
    if (vnode->ops && vnode->ops->close) {
        vnode->ops->close(vnode);
    }
    
    vfs_vnode_release(vnode);
    kfree(file);
    fd_table[fd] = NULL;
    
    return VFS_OK;
}

int vfs_read(int fd, void* buffer, uint32_t size) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd] || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    // Validate size is reasonable (256MB limit)
    if (size > 0x10000000) {
        serial_puts("VFS: vfs_read - size too large\n");
        return VFS_ERR_INVALID;
    }
    
    file_t* file = fd_table[fd];
    vnode_t* vnode = file->vnode;
    
    // Check file permission - need VIEW access to read
    process_t* proc = process_get_current();
    if (proc && !vfs_check_access(vnode, proc->owner_id, proc->owner_type, CHECK_VIEW)) {
        return VFS_ERR_PERM;
    }
    
    // Check read permissions
    if ((file->flags & O_WRONLY) && !(file->flags & O_RDWR)) {
        return VFS_ERR_PERM;
    }
    
    // Check if it's a directory
    if (vnode->type == VFS_DIRECTORY) {
        return VFS_ERR_ISDIR;
    }
    
    // Call vnode read operation
    if (!vnode->ops || !vnode->ops->read) {
        return VFS_ERR_IO;
    }
    
    int bytes_read = vnode->ops->read(vnode, buffer, size, file->offset);
    if (bytes_read > 0) {
        file->offset += bytes_read;
    }
    
    return bytes_read;
}

int vfs_write(int fd, const void* buffer, uint32_t size) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd] || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    // Validate size is reasonable (256MB limit)
    if (size > 0x10000000) {
        serial_puts("VFS: vfs_write - size too large\n");
        return VFS_ERR_INVALID;
    }
    
    file_t* file = fd_table[fd];
    vnode_t* vnode = file->vnode;
    
    // Check file permission - need MODIFY access to write
    process_t* proc = process_get_current();
    if (proc && !vfs_check_access(vnode, proc->owner_id, proc->owner_type, CHECK_MODIFY)) {
        return VFS_ERR_PERM;
    }
    
    // Check write permissions
    if ((file->flags & O_RDONLY) && !(file->flags & O_RDWR)) {
        return VFS_ERR_PERM;
    }
    
    // Check if it's a directory
    if (vnode->type == VFS_DIRECTORY) {
        return VFS_ERR_ISDIR;
    }
    
    // Call vnode write operation
    if (!vnode->ops || !vnode->ops->write) {
        return VFS_ERR_IO;
    }
    
    int bytes_written = vnode->ops->write(vnode, buffer, size, file->offset);
    if (bytes_written > 0) {
        file->offset += bytes_written;
    }
    
    return bytes_written;
}

int vfs_lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) {
        return VFS_ERR_INVALID;
    }
    
    file_t* file = fd_table[fd];
    vnode_t* vnode = file->vnode;
    
    int new_offset;
    
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = file->offset + offset;
            break;
        case SEEK_END:
            new_offset = vnode->size + offset;
            break;
        default:
            return VFS_ERR_INVALID;
    }
    
    if (new_offset < 0) {
        return VFS_ERR_INVALID;
    }
    
    file->offset = new_offset;
    return new_offset;
}

int vfs_readdir(int fd, dirent_t* dirent) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd] || !dirent) {
        return VFS_ERR_INVALID;
    }
    
    file_t* file = fd_table[fd];
    vnode_t* vnode = file->vnode;
    
    // Check if it's a directory
    if (vnode->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    // Check permission - need VIEW access to read directory
    process_t* proc = process_get_current();
    if (proc && !vfs_check_access(vnode, proc->owner_id, proc->owner_type, CHECK_VIEW)) {
        return VFS_ERR_PERM;
    }
    
    // Call vnode readdir operation
    if (!vnode->ops || !vnode->ops->readdir) {
        return VFS_ERR_IO;
    }
    
    int ret = vnode->ops->readdir(vnode, file->offset, dirent);
    if (ret == VFS_OK) {
        file->offset++;
    }
    
    return ret;
}

int vfs_mkdir(const char* path) {
    if (!path) {
        serial_puts("vfs_mkdir: path is NULL\n");
        return VFS_ERR_INVALID;
    }
    
    serial_puts("vfs_mkdir: creating directory '");
    serial_puts(path);
    serial_puts("'\n");
    
    // Extract parent directory and dirname
    char* path_copy = vfs_normalize_path(path);
    if (!path_copy) {
        serial_puts("vfs_mkdir: normalize_path failed\n");
        return VFS_ERR_NOSPACE;
    }
    
    serial_puts("vfs_mkdir: normalized to '");
    serial_puts(path_copy);
    serial_puts("'\n");
    
    char* last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        serial_puts("vfs_mkdir: no slash found\n");
        kfree(path_copy);
        return VFS_ERR_INVALID;
    }
    
    *last_slash = '\0';
    const char* dirname = last_slash + 1;
    const char* parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    
    serial_puts("vfs_mkdir: parent_path='");
    serial_puts(parent_path);
    serial_puts("', dirname='");
    serial_puts(dirname);
    serial_puts("'\n");
    
    vnode_t* parent = vfs_resolve_path(parent_path);
    if (!parent) {
        serial_puts("vfs_mkdir: parent not found\n");
        kfree(path_copy);
        return VFS_ERR_NOTFOUND;
    }
    
    // Check permissions - need modify access to parent directory (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        if (!vfs_check_access(parent, proc->owner_id, proc->owner_type, CHECK_MODIFY)) {
            serial_puts("vfs_mkdir: permission denied\n");
            kfree(path_copy);
            return VFS_ERR_PERM;
        }
    }
    
    if (!parent->ops) {
        serial_puts("vfs_mkdir: parent->ops is NULL\n");
        kfree(path_copy);
        return VFS_ERR_NOTFOUND;
    }
    
    if (!parent->ops->mkdir) {
        serial_puts("vfs_mkdir: parent->ops->mkdir is NULL\n");
        kfree(path_copy);
        return VFS_ERR_NOTFOUND;
    }
    
    serial_puts("vfs_mkdir: calling parent->ops->mkdir\n");
    int ret = parent->ops->mkdir(parent, dirname);
    
    char buf[16];
    serial_puts("vfs_mkdir: mkdir returned ");
    itoa(ret, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    kfree(path_copy);
    
    return ret;
}

int vfs_rmdir(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    // Check if trying to remove root
    if (strcmp(path, "/") == 0) {
        return VFS_ERR_PERM;
    }
    
    // First verify it's a directory
    vnode_t* target = vfs_resolve_path(path);
    if (!target) {
        return VFS_ERR_NOTFOUND;
    }
    
    if (target->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    // Check if current process can delete this directory (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        if (!vfs_check_access(target, proc->owner_id, proc->owner_type, CHECK_DELETE)) {
            return VFS_ERR_PERM;
        }
    }
    
    // Extract parent directory and dirname
    char* path_copy = vfs_normalize_path(path);
    if (!path_copy) {
        return VFS_ERR_NOSPACE;
    }
    
    char* last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        kfree(path_copy);
        return VFS_ERR_INVALID;
    }
    
    *last_slash = '\0';
    const char* dirname = last_slash + 1;
    const char* parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    
    vnode_t* parent = vfs_resolve_path(parent_path);
    if (!parent || !parent->ops || !parent->ops->unlink) {
        kfree(path_copy);
        return VFS_ERR_NOTFOUND;
    }
    
    // Check parent directory permissions (v0.7.3)
    if (proc) {
        if (!vfs_check_access(parent, proc->owner_id, proc->owner_type, CHECK_MODIFY)) {
            kfree(path_copy);
            return VFS_ERR_PERM;
        }
    }
    
    // Use unlink operation - the filesystem should handle directory-specific checks
    int ret = parent->ops->unlink(parent, dirname);
    kfree(path_copy);
    
    return ret;
}

int vfs_unlink(const char* path) {
    if (!path) {
        return VFS_ERR_INVALID;
    }
    
    // Extract parent directory and filename
    char* path_copy = vfs_normalize_path(path);
    if (!path_copy) {
        return VFS_ERR_NOSPACE;
    }
    
    char* last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        kfree(path_copy);
        return VFS_ERR_INVALID;
    }
    
    *last_slash = '\0';
    const char* filename = last_slash + 1;
    const char* parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    
    // Check if file exists and get permissions (v0.7.3)
    vnode_t* target = vfs_resolve_path(path);
    int can_delete_file = 0;
    
    if (target) {
        process_t* proc = process_get_current();
        if (proc) {
            // Check if user can delete the file itself
            can_delete_file = vfs_check_access(target, proc->owner_id, proc->owner_type, CHECK_DELETE);
            if (!can_delete_file) {
                kfree(path_copy);
                return VFS_ERR_PERM;
            }
        }
    }
    
    vnode_t* parent = vfs_resolve_path(parent_path);
    if (!parent || !parent->ops || !parent->ops->unlink) {
        kfree(path_copy);
        return VFS_ERR_NOTFOUND;
    }
    
    // Check parent directory permissions (v0.7.3)
    // If user can delete the file, they don't need parent modify permission
    // Otherwise, they need parent modify permission to remove entries
    if (!can_delete_file) {
        process_t* proc = process_get_current();
        if (proc) {
            if (!vfs_check_access(parent, proc->owner_id, proc->owner_type, CHECK_MODIFY)) {
                kfree(path_copy);
                return VFS_ERR_PERM;
            }
        }
    }
    
    int ret = parent->ops->unlink(parent, filename);
    kfree(path_copy);
    
    return ret;
}

int vfs_stat(const char* path, stat_t* stat) {
    if (!path || !stat) {
        return VFS_ERR_INVALID;
    }
    
    vnode_t* vnode = vfs_resolve_path(path);
    if (!vnode) {
        return VFS_ERR_NOTFOUND;
    }
    
    if (vnode->ops && vnode->ops->stat) {
        return vnode->ops->stat(vnode, stat);
    }
    
    // Default stat implementation
    stat->st_dev = 0;
    stat->st_ino = vnode->inode;
    stat->st_mode = vnode->type;
    stat->st_nlink = 1;
    stat->st_uid = 0;
    stat->st_gid = 0;
    stat->st_rdev = 0;
    stat->st_size = vnode->size;
    stat->st_blksize = 512;
    stat->st_blocks = (vnode->size + 511) / 512;
    
    return VFS_OK;
}

const char* vfs_getcwd(void) {
    if (!cwd) {
        return "/";
    }
    return cwd_path;
}

int vfs_chdir(const char* path) {
    if (!path || !*path) {
        return VFS_ERR_INVALID;
    }
    
    // Normalize the path to get the absolute path
    char* norm_path = vfs_normalize_path(path);
    if (!norm_path) {
        return VFS_ERR_NOSPACE;
    }
    
    // Resolve the path
    vnode_t* target = vfs_resolve_path(norm_path);
    if (!target) {
        kfree(norm_path);
        return VFS_ERR_NOTFOUND;
    }
    
    // Check if it's a directory
    if (target->type != VFS_DIRECTORY) {
        kfree(norm_path);
        return VFS_ERR_NOTDIR;
    }
    
    // Release old cwd if exists (but don't decrease refcount for root)
    if (cwd && cwd != root_vnode) {
        vfs_vnode_release(cwd);
    }
    
    // Set new cwd
    cwd = target;
    vfs_vnode_acquire(cwd);
    
    // Update the path string
    strncpy(cwd_path, norm_path, 255);
    cwd_path[255] = '\0';
    
    kfree(norm_path);
    return VFS_OK;
}

void vfs_vnode_acquire(vnode_t* vnode) {
    if (vnode) {
        vnode->refcount++;
    }
}

void vfs_vnode_release(vnode_t* vnode) {
    if (vnode) {
        if (vnode->refcount > 0) {
            vnode->refcount--;
        }
        // Note:don't free vnodes automatically for now
        // we'd free when refcount reaches 0 later
    }
}

file_t* vfs_get_file(int fd) {
    if (fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }
    return fd_table[fd];
}

mount_t* vfs_get_mount(int index) {
    if (index < 0 || index >= MAX_MOUNTS) {
        return NULL;
    }
    if (mount_table[index].fs == NULL) {
        return NULL;
    }
    return &mount_table[index];
}

// Permission checking (v0.7.3)
int vfs_check_access(vnode_t* vnode, uint32_t requester_id, 
                     owner_type_t requester_type, access_check_t check) {
    if (!vnode) {
        return 0;
    }
    return fileperm_check(&vnode->access, requester_id, requester_type, check);
}

// Set file owner
int vfs_set_owner(const char* path, uint32_t owner_id, owner_type_t owner_type) {
    if (!path) {
        return VFS_ERR_INVALID;
    }

    vnode_t* vnode = vfs_resolve_path(path);
    if (!vnode) {
        return VFS_ERR_NOTFOUND;
    }

    // Only current owner or system can change ownership
    // This check would use the current process info (to be added)
    
    vnode->access.owner_id = owner_id;
    vnode->access.owner_type = owner_type;
    
    return VFS_OK;
}

// Set file access bits
int vfs_set_access(const char* path, uint8_t owner_bits, uint8_t other_bits) {
    if (!path) {
        return VFS_ERR_INVALID;
    }

    vnode_t* vnode = vfs_resolve_path(path);
    if (!vnode) {
        return VFS_ERR_NOTFOUND;
    }

    // Only owner or system can change access bits
    // This check would use the current process info (to be added)
    
    vnode->access.owner_access = owner_bits;
    vnode->access.other_access = other_bits;
    
    return VFS_OK;
}

// Get file access info
int vfs_get_access(const char* path, file_access_t* access) {
    if (!path || !access) {
        return VFS_ERR_INVALID;
    }

    vnode_t* vnode = vfs_resolve_path(path);
    if (!vnode) {
        return VFS_ERR_NOTFOUND;
    }

    *access = vnode->access;
    return VFS_OK;
}
