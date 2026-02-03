/*
 * === AOS HEADER BEGIN ===
 * ./src/fs/ramfs.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <fs/ramfs.h>
#include <fs/vfs.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <vmm.h>
#include <fileperm.h>
#include <process.h>

// Ramfs-specific structures

#define RAMFS_MAX_FILES 128
#define RAMFS_MAX_FILE_SIZE (1024 * 1024) // 1MB per file

typedef struct ramfs_file {
    char name[256];
    uint32_t inode;
    uint32_t type;
    uint32_t size;
    uint32_t capacity;
    uint8_t* data;
    struct ramfs_file* parent;
    struct ramfs_file* children[RAMFS_MAX_FILES];
    uint32_t num_children;
} ramfs_file_t;

typedef struct ramfs_data {
    ramfs_file_t* root;
    uint32_t next_inode;
} ramfs_data_t;

// Static ramfs data - avoid kmalloc during early boot
static ramfs_data_t ramfs_filesystem_data;
static ramfs_file_t ramfs_root_file;
static ramfs_file_t ramfs_files[RAMFS_MAX_FILES];
static uint32_t ramfs_files_used = 0;

// Forward declarations
static int ramfs_vnode_open(vnode_t* node, uint32_t flags);
static int ramfs_vnode_close(vnode_t* node);
static int ramfs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset);
static int ramfs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset);
static vnode_t* ramfs_vnode_finddir(vnode_t* node, const char* name);
static vnode_t* ramfs_vnode_create(vnode_t* parent, const char* name, uint32_t flags);
static int ramfs_vnode_unlink(vnode_t* parent, const char* name);
static int ramfs_vnode_mkdir(vnode_t* parent, const char* name);
static int ramfs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent);
static int ramfs_vnode_stat(vnode_t* node, stat_t* stat);

// VFS operations
static vnode_ops_t ramfs_vnode_ops = {
    .open = ramfs_vnode_open,
    .close = ramfs_vnode_close,
    .read = ramfs_vnode_read,
    .write = ramfs_vnode_write,
    .finddir = ramfs_vnode_finddir,
    .create = ramfs_vnode_create,
    .unlink = ramfs_vnode_unlink,
    .mkdir = ramfs_vnode_mkdir,
    .readdir = ramfs_vnode_readdir,
    .stat = ramfs_vnode_stat
};

// Filesystem operations
static int ramfs_mount(filesystem_t* fs, const char* source, uint32_t flags);
static int ramfs_unmount(filesystem_t* fs);
static vnode_t* ramfs_get_root(filesystem_t* fs);

static filesystem_ops_t ramfs_fs_ops = {
    .mount = ramfs_mount,
    .unmount = ramfs_unmount,
    .get_root = ramfs_get_root
};

static filesystem_t ramfs_filesystem = {
    .name = "ramfs",
    .ops = &ramfs_fs_ops,
    .fs_data = NULL,
    .mount = NULL
};

// Helper functions

static ramfs_file_t* ramfs_create_file(ramfs_data_t* fs_data, const char* name, uint32_t type) {
    if (ramfs_files_used >= RAMFS_MAX_FILES) {
        serial_puts("ramfs: out of file slots\n");
        return NULL;
    }
    
    ramfs_file_t* file = &ramfs_files[ramfs_files_used++];
    
    strncpy(file->name, name, 255);
    file->name[255] = '\0';
    file->inode = fs_data->next_inode++;
    file->type = type;
    file->size = 0;
    file->capacity = 0;
    file->data = NULL;
    file->parent = NULL;
    file->num_children = 0;
    
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        file->children[i] = NULL;
    }
    
    return file;
}

static void ramfs_free_file(ramfs_file_t* file) {
    if (!file) {
        return;
    }
    
    // Free data (still dynamically allocated for file contents)
    if (file->data) {
        kfree(file->data);
        file->data = NULL;
    }
    
    // Note: File structures themselves are statically allocated,
    // so we don't free them. In a complete implementation,
    // we'd mark them as available for reuse.
}

static vnode_t* ramfs_file_to_vnode(ramfs_file_t* file, filesystem_t* fs) {
    if (!file) {
        return NULL;
    }
    
    // For now, allocate vnodes dynamically - we'll fix this later if needed
    vnode_t* vnode = (vnode_t*)kmalloc(sizeof(vnode_t));
    if (!vnode) {
        return NULL;
    }
    
    strncpy(vnode->name, file->name, 255);
    vnode->name[255] = '\0';
    vnode->inode = file->inode;
    vnode->type = file->type;
    vnode->size = file->size;
    vnode->flags = 0;
    vnode->refcount = 0;
    vnode->fs = fs;
    vnode->mount = NULL;
    vnode->fs_data = file;
    vnode->ops = &ramfs_vnode_ops;
    
    // Set permissions based on current process owner (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        // File created by current process - set owner accordingly
        if (file->type == VFS_DIRECTORY) {
            vnode->access = fileperm_default_dir(proc->owner_id, proc->owner_type);
        } else {
            vnode->access = fileperm_default_file(proc->owner_id, proc->owner_type);
        }
    } else {
        // No current process - system owned
        if (file->type == VFS_DIRECTORY) {
            vnode->access = fileperm_default_dir(0, OWNER_SYSTEM);
        } else {
            vnode->access = fileperm_default_file(0, OWNER_SYSTEM);
        }
    }
    
    return vnode;
}

// Filesystem operations implementation

static int ramfs_mount(filesystem_t* fs, const char* source, uint32_t flags) {
    (void)source;
    (void)flags;
    
    // Use static ramfs data
    ramfs_data_t* fs_data = &ramfs_filesystem_data;
    fs_data->next_inode = 1;
    
    // Create root directory using static allocation
    fs_data->root = &ramfs_root_file;
    
    strncpy(fs_data->root->name, "/", 255);
    fs_data->root->name[255] = '\0';
    fs_data->root->inode = fs_data->next_inode++;
    fs_data->root->type = VFS_DIRECTORY;
    fs_data->root->size = 0;
    fs_data->root->capacity = 0;
    fs_data->root->data = NULL;
    fs_data->root->parent = NULL;
    fs_data->root->num_children = 0;
    
    for (uint32_t i = 0; i < RAMFS_MAX_FILES; i++) {
        fs_data->root->children[i] = NULL;
    }
    
    fs->fs_data = fs_data;
    
    serial_puts("Ramfs mounted.\n");
    return VFS_OK;
}

static int ramfs_unmount(filesystem_t* fs) {
    if (!fs || !fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    ramfs_data_t* fs_data = (ramfs_data_t*)fs->fs_data;
    
    // Free all files
    ramfs_free_file(fs_data->root);
    kfree(fs_data);
    
    fs->fs_data = NULL;
    
    return VFS_OK;
}

static vnode_t* ramfs_get_root(filesystem_t* fs) {
    if (!fs || !fs->fs_data) {
        return NULL;
    }
    
    ramfs_data_t* fs_data = (ramfs_data_t*)fs->fs_data;
    return ramfs_file_to_vnode(fs_data->root, fs);
}

// Vnode operations implementation

static int ramfs_vnode_open(vnode_t* node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static int ramfs_vnode_close(vnode_t* node) {
    (void)node;
    return VFS_OK;
}

static int ramfs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset) {
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    ramfs_file_t* file = (ramfs_file_t*)node->fs_data;
    if (!file) {
        return VFS_ERR_INVALID;
    }
    
    // Check if offset is beyond file size
    if (offset >= file->size) {
        return 0;
    }
    
    // Calculate bytes to read
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > file->size) {
        bytes_to_read = file->size - offset;
    }
    
    // Copy data
    if (file->data) {
        memcpy(buffer, file->data + offset, bytes_to_read);
    }
    
    return bytes_to_read;
}

static int ramfs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset) {
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    ramfs_file_t* file = (ramfs_file_t*)node->fs_data;
    if (!file) {
        return VFS_ERR_INVALID;
    }
    
    // Calculate new size needed
    uint32_t new_size = offset + size;
    
    // Check if we need to expand
    if (new_size > file->capacity) {
        // Round up to nearest 4KB
        uint32_t new_capacity = (new_size + 4095) & ~4095;
        
        // Check max file size
        if (new_capacity > RAMFS_MAX_FILE_SIZE) {
            return VFS_ERR_NOSPACE;
        }
        
        // Allocate new buffer
        uint8_t* new_data = (uint8_t*)kmalloc(new_capacity);
        if (!new_data) {
            return VFS_ERR_NOSPACE;
        }
        
        // Copy old data
        if (file->data) {
            memcpy(new_data, file->data, file->size);
            kfree(file->data);
        }
        
        file->data = new_data;
        file->capacity = new_capacity;
    }
    
    // Write data
    memcpy(file->data + offset, buffer, size);
    
    // Update size if we wrote past the end
    if (new_size > file->size) {
        file->size = new_size;
        node->size = new_size;
    }
    
    return size;
}

static vnode_t* ramfs_vnode_finddir(vnode_t* node, const char* name) {
    if (!node || !name) {
        return NULL;
    }
    
    ramfs_file_t* dir = (ramfs_file_t*)node->fs_data;
    if (!dir || dir->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    // Search for child with matching name
    for (uint32_t i = 0; i < dir->num_children; i++) {
        if (dir->children[i] && strcmp(dir->children[i]->name, name) == 0) {
            return ramfs_file_to_vnode(dir->children[i], node->fs);
        }
    }
    
    return NULL;
}

static vnode_t* ramfs_vnode_create(vnode_t* parent, const char* name, uint32_t flags) {
    (void)flags;
    
    if (!parent || !name) {
        return NULL;
    }
    
    ramfs_file_t* parent_file = (ramfs_file_t*)parent->fs_data;
    if (!parent_file || parent_file->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    // Check if already exists
    for (uint32_t i = 0; i < parent_file->num_children; i++) {
        if (parent_file->children[i] && strcmp(parent_file->children[i]->name, name) == 0) {
            return NULL; // Already exists
        }
    }
    
    // Check if we have space
    if (parent_file->num_children >= RAMFS_MAX_FILES) {
        return NULL;
    }
    
    // Get filesystem data
    ramfs_data_t* fs_data = (ramfs_data_t*)parent->fs->fs_data;
    if (!fs_data) {
        return NULL;
    }
    
    // Create new file
    ramfs_file_t* file = ramfs_create_file(fs_data, name, VFS_FILE);
    if (!file) {
        return NULL;
    }
    
    file->parent = parent_file;
    parent_file->children[parent_file->num_children++] = file;
    
    return ramfs_file_to_vnode(file, parent->fs);
}

static int ramfs_vnode_unlink(vnode_t* parent, const char* name) {
    if (!parent || !name) {
        return VFS_ERR_INVALID;
    }
    
    ramfs_file_t* parent_file = (ramfs_file_t*)parent->fs_data;
    if (!parent_file || parent_file->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    // Find child
    for (uint32_t i = 0; i < parent_file->num_children; i++) {
        if (parent_file->children[i] && strcmp(parent_file->children[i]->name, name) == 0) {
            ramfs_file_t* file = parent_file->children[i];
            
            // Check if it's a directory with children
            if (file->type == VFS_DIRECTORY && file->num_children > 0) {
                return VFS_ERR_NOTEMPTY;
            }
            
            // Remove from parent's children
            for (uint32_t j = i; j < parent_file->num_children - 1; j++) {
                parent_file->children[j] = parent_file->children[j + 1];
            }
            parent_file->children[--parent_file->num_children] = NULL;
            
            // Free the file
            ramfs_free_file(file);
            
            return VFS_OK;
        }
    }
    
    return VFS_ERR_NOTFOUND;
}

static int ramfs_vnode_mkdir(vnode_t* parent, const char* name) {
    if (!parent || !name) {
        return VFS_ERR_INVALID;
    }
    
    ramfs_file_t* parent_file = (ramfs_file_t*)parent->fs_data;
    if (!parent_file || parent_file->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    // Check if already exists
    for (uint32_t i = 0; i < parent_file->num_children; i++) {
        if (parent_file->children[i] && strcmp(parent_file->children[i]->name, name) == 0) {
            return VFS_ERR_EXISTS;
        }
    }
    
    // Check if we have space
    if (parent_file->num_children >= RAMFS_MAX_FILES) {
        return VFS_ERR_NOSPACE;
    }
    
    // Get filesystem data
    ramfs_data_t* fs_data = (ramfs_data_t*)parent->fs->fs_data;
    if (!fs_data) {
        return VFS_ERR_INVALID;
    }
    
    // Create new directory
    ramfs_file_t* dir = ramfs_create_file(fs_data, name, VFS_DIRECTORY);
    if (!dir) {
        return VFS_ERR_NOSPACE;
    }
    
    dir->parent = parent_file;
    parent_file->children[parent_file->num_children++] = dir;
    
    return VFS_OK;
}

static int ramfs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent) {
    if (!node || !dirent) {
        return VFS_ERR_INVALID;
    }
    
    ramfs_file_t* dir = (ramfs_file_t*)node->fs_data;
    if (!dir || dir->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    if (index >= dir->num_children) {
        return VFS_ERR_NOTFOUND;
    }
    
    ramfs_file_t* child = dir->children[index];
    if (!child) {
        return VFS_ERR_NOTFOUND;
    }
    
    strncpy(dirent->name, child->name, 255);
    dirent->name[255] = '\0';
    dirent->inode = child->inode;
    dirent->type = child->type;
    
    return VFS_OK;
}

static int ramfs_vnode_stat(vnode_t* node, stat_t* stat) {
    if (!node || !stat) {
        return VFS_ERR_INVALID;
    }
    
    ramfs_file_t* file = (ramfs_file_t*)node->fs_data;
    if (!file) {
        return VFS_ERR_INVALID;
    }
    
    stat->st_dev = 0;
    stat->st_ino = file->inode;
    stat->st_mode = file->type;
    stat->st_nlink = 1;
    stat->st_uid = 0;
    stat->st_gid = 0;
    stat->st_rdev = 0;
    stat->st_size = file->size;
    stat->st_blksize = 512;
    stat->st_blocks = (file->size + 511) / 512;
    
    return VFS_OK;
}

// Public API

void ramfs_init(void) {
    serial_puts("Initializing ramfs...\n");
    vfs_register_filesystem(&ramfs_filesystem);
}

filesystem_t* ramfs_get_fs(void) {
    return &ramfs_filesystem;
}
