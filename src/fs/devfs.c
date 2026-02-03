/*
 * === AOS HEADER BEGIN ===
 * ./src/fs/devfs.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <fs/devfs.h>
#include <string.h>
#include <serial.h>
#include <arch/i386/pit.h>
#include <stdint.h>
#include <stdlib.h>
#include <vmm.h>

// Simple device kinds
typedef enum {
    DEVFS_NODE_ROOT = 0,
    DEVFS_NODE_NULL,
    DEVFS_NODE_ZERO,
    DEVFS_NODE_RANDOM
} devfs_node_type_t;

typedef struct devfs_node {
    devfs_node_type_t type;
    uint32_t inode;
} devfs_node_t;

static vnode_t* devfs_make_vnode(const char* name, devfs_node_type_t type, filesystem_t* fs);

static int devfs_vnode_open(vnode_t* node, uint32_t flags);
static int devfs_vnode_close(vnode_t* node);
static int devfs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset);
static int devfs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset);
static vnode_t* devfs_vnode_finddir(vnode_t* node, const char* name);
static vnode_t* devfs_vnode_create(vnode_t* parent, const char* name, uint32_t flags);
static int devfs_vnode_unlink(vnode_t* parent, const char* name);
static int devfs_vnode_mkdir(vnode_t* parent, const char* name);
static int devfs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent);
static int devfs_vnode_stat(vnode_t* node, stat_t* stat);

static vnode_ops_t devfs_vnode_ops = {
    .open = devfs_vnode_open,
    .close = devfs_vnode_close,
    .read = devfs_vnode_read,
    .write = devfs_vnode_write,
    .finddir = devfs_vnode_finddir,
    .create = devfs_vnode_create,
    .unlink = devfs_vnode_unlink,
    .mkdir = devfs_vnode_mkdir,
    .readdir = devfs_vnode_readdir,
    .stat = devfs_vnode_stat
};

static int devfs_mount(filesystem_t* fs, const char* source, uint32_t flags);
static int devfs_unmount(filesystem_t* fs);
static vnode_t* devfs_get_root(filesystem_t* fs);

static filesystem_ops_t devfs_fs_ops = {
    .mount = devfs_mount,
    .unmount = devfs_unmount,
    .get_root = devfs_get_root
};

static filesystem_t devfs_fs = {
    .name = "devfs",
    .ops = &devfs_fs_ops,
    .fs_data = NULL,
    .mount = NULL
};

static devfs_node_t devfs_root_node;
static uint32_t devfs_next_inode = 1;
static uint32_t devfs_rng_state = 0x9E3779B9;

static uint32_t devfs_rand32(void) {
    devfs_rng_state ^= devfs_rng_state << 13;
    devfs_rng_state ^= devfs_rng_state >> 17;
    devfs_rng_state ^= devfs_rng_state << 5;
    return devfs_rng_state;
}

static void devfs_mix_seed(uint32_t extra) {
    devfs_rng_state ^= extra + (uint32_t)((uintptr_t)&devfs_rng_state);
    devfs_rand32();
}

static vnode_t* devfs_make_vnode(const char* name, devfs_node_type_t type, filesystem_t* fs) {
    vnode_t* vnode = (vnode_t*)kmalloc(sizeof(vnode_t));
    if (!vnode) {
        return NULL;
    }

    devfs_node_t* node = (devfs_node_t*)kmalloc(sizeof(devfs_node_t));
    if (!node) {
        kfree(vnode);
        return NULL;
    }

    node->type = type;
    node->inode = devfs_next_inode++;

    strncpy(vnode->name, name, 255);
    vnode->name[255] = '\0';
    vnode->inode = node->inode;
    vnode->type = (type == DEVFS_NODE_ROOT) ? VFS_DIRECTORY : VFS_CHARDEV;
    vnode->size = 0;
    vnode->flags = 0;
    vnode->refcount = 0;
    vnode->fs = fs;
    vnode->mount = NULL;
    vnode->fs_data = node;
    vnode->ops = &devfs_vnode_ops;

    return vnode;
}

static int devfs_vnode_open(vnode_t* node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static int devfs_vnode_close(vnode_t* node) {
    (void)node;
    return VFS_OK;
}

static int devfs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset) {
    (void)offset;
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }

    devfs_node_t* dev = (devfs_node_t*)node->fs_data;
    if (!dev) {
        return VFS_ERR_INVALID;
    }

    switch (dev->type) {
        case DEVFS_NODE_NULL:
            return 0;
        case DEVFS_NODE_ZERO:
            memset(buffer, 0, size);
            return (int)size;
        case DEVFS_NODE_RANDOM: {
            uint8_t* out = (uint8_t*)buffer;
            for (uint32_t i = 0; i < size; i++) {
                if ((i & 3) == 0) {
                    devfs_mix_seed(system_ticks);
                }
                out[i] = (uint8_t)(devfs_rand32() & 0xFF);
            }
            return (int)size;
        }
        case DEVFS_NODE_ROOT:
        default:
            return VFS_ERR_ISDIR;
    }
}

static int devfs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset) {
    (void)offset;
    devfs_node_t* dev = (devfs_node_t*)node->fs_data;
    if (!dev) {
        return VFS_ERR_INVALID;
    }

    switch (dev->type) {
        case DEVFS_NODE_NULL:
        case DEVFS_NODE_ZERO:
            return (int)size;
        case DEVFS_NODE_RANDOM: {
            const uint8_t* data = (const uint8_t*)buffer;
            for (uint32_t i = 0; i < size; i++) {
                devfs_mix_seed((uint32_t)(data ? data[i] : 0));
            }
            return (int)size;
        }
        case DEVFS_NODE_ROOT:
        default:
            return VFS_ERR_ISDIR;
    }
}

static vnode_t* devfs_vnode_finddir(vnode_t* node, const char* name) {
    devfs_node_t* dev = (devfs_node_t*)node->fs_data;
    if (!dev || dev->type != DEVFS_NODE_ROOT || !name) {
        return NULL;
    }

    if (strcmp(name, "null") == 0) {
        return devfs_make_vnode("null", DEVFS_NODE_NULL, node->fs);
    }
    if (strcmp(name, "zero") == 0) {
        return devfs_make_vnode("zero", DEVFS_NODE_ZERO, node->fs);
    }
    if (strcmp(name, "random") == 0) {
        return devfs_make_vnode("random", DEVFS_NODE_RANDOM, node->fs);
    }

    return NULL;
}

static vnode_t* devfs_vnode_create(vnode_t* parent, const char* name, uint32_t flags) {
    (void)parent;
    (void)name;
    (void)flags;
    return NULL;
}

static int devfs_vnode_unlink(vnode_t* parent, const char* name) {
    (void)parent;
    (void)name;
    return VFS_ERR_PERM;
}

static int devfs_vnode_mkdir(vnode_t* parent, const char* name) {
    (void)parent;
    (void)name;
    return VFS_ERR_PERM;
}

static int devfs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent) {
    devfs_node_t* dev = (devfs_node_t*)node->fs_data;
    if (!dev || dev->type != DEVFS_NODE_ROOT || !dirent) {
        return VFS_ERR_INVALID;
    }

    const char* names[] = {"null", "zero", "random"};
    if (index >= (sizeof(names) / sizeof(names[0]))) {
        return VFS_ERR_NOTFOUND;
    }

    const char* entry = names[index];
    strncpy(dirent->name, entry, 255);
    dirent->name[255] = '\0';
    dirent->inode = index + 1;
    dirent->type = VFS_CHARDEV;

    return VFS_OK;
}

static int devfs_vnode_stat(vnode_t* node, stat_t* stat) {
    devfs_node_t* dev = (devfs_node_t*)node->fs_data;
    if (!dev || !stat) {
        return VFS_ERR_INVALID;
    }

    stat->st_dev = 0;
    stat->st_ino = dev->inode;
    stat->st_mode = (dev->type == DEVFS_NODE_ROOT) ? VFS_DIRECTORY : VFS_CHARDEV;
    stat->st_nlink = 1;
    stat->st_uid = 0;
    stat->st_gid = 0;
    stat->st_rdev = 0;
    stat->st_size = 0;
    stat->st_blksize = 512;
    stat->st_blocks = 0;

    return VFS_OK;
}

static int devfs_mount(filesystem_t* fs, const char* source, uint32_t flags) {
    (void)source;
    (void)flags;

    devfs_root_node.type = DEVFS_NODE_ROOT;
    devfs_root_node.inode = 0;
    fs->fs_data = &devfs_root_node;

    devfs_mix_seed(system_ticks ^ PIT_BASE_FREQUENCY);

    return VFS_OK;
}

static int devfs_unmount(filesystem_t* fs) {
    (void)fs;
    return VFS_OK;
}

static vnode_t* devfs_get_root(filesystem_t* fs) {
    return devfs_make_vnode("/", DEVFS_NODE_ROOT, fs);
}

void devfs_init(void) {
    serial_puts("Initializing devfs...\n");
    vfs_register_filesystem(&devfs_fs);
}

filesystem_t* devfs_get_fs(void) {
    return &devfs_fs;
}
