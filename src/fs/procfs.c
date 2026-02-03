/*
 * === AOS HEADER BEGIN ===
 * ./src/fs/procfs.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <fs/procfs.h>
#include <string.h>
#include <serial.h>
#include <pmm.h>
#include <arch/i386/pit.h>
#include <process.h>
#include <stdlib.h>
#include <stdint.h>
#include <vmm.h>

typedef enum {
    PROC_NODE_ROOT = 0,
    PROC_NODE_MEMINFO,
    PROC_NODE_CPUINFO,
    PROC_NODE_PID_DIR,
    PROC_NODE_PID_INFO
} proc_node_type_t;

typedef struct proc_node {
    proc_node_type_t type;
    pid_t pid;
    uint32_t inode;
} proc_node_t;

static vnode_t* procfs_make_vnode(const char* name, proc_node_type_t type, pid_t pid, filesystem_t* fs);

static int procfs_vnode_open(vnode_t* node, uint32_t flags);
static int procfs_vnode_close(vnode_t* node);
static int procfs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset);
static int procfs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset);
static vnode_t* procfs_vnode_finddir(vnode_t* node, const char* name);
static vnode_t* procfs_vnode_create(vnode_t* parent, const char* name, uint32_t flags);
static int procfs_vnode_unlink(vnode_t* parent, const char* name);
static int procfs_vnode_mkdir(vnode_t* parent, const char* name);
static int procfs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent);
static int procfs_vnode_stat(vnode_t* node, stat_t* stat);

static vnode_ops_t procfs_vnode_ops = {
    .open = procfs_vnode_open,
    .close = procfs_vnode_close,
    .read = procfs_vnode_read,
    .write = procfs_vnode_write,
    .finddir = procfs_vnode_finddir,
    .create = procfs_vnode_create,
    .unlink = procfs_vnode_unlink,
    .mkdir = procfs_vnode_mkdir,
    .readdir = procfs_vnode_readdir,
    .stat = procfs_vnode_stat
};

static int procfs_mount(filesystem_t* fs, const char* source, uint32_t flags);
static int procfs_unmount(filesystem_t* fs);
static vnode_t* procfs_get_root(filesystem_t* fs);

static filesystem_ops_t procfs_fs_ops = {
    .mount = procfs_mount,
    .unmount = procfs_unmount,
    .get_root = procfs_get_root
};

static filesystem_t procfs_fs = {
    .name = "procfs",
    .ops = &procfs_fs_ops,
    .fs_data = NULL,
    .mount = NULL
};

static proc_node_t procfs_root_node;
static uint32_t procfs_next_inode = 1;

static const char* proc_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_READY: return "ready";
        case PROCESS_RUNNING: return "run";
        case PROCESS_BLOCKED: return "wait";
        case PROCESS_SLEEPING: return "sleep";
        case PROCESS_ZOMBIE: return "zombie";
        case PROCESS_DEAD: return "dead";
        default: return "unknown";
    }
}

static uint32_t str_append(char* dst, uint32_t cap, uint32_t pos, const char* text) {
    if (!dst || !text || pos >= cap) {
        return pos;
    }
    while (*text && pos + 1 < cap) {
        dst[pos++] = *text++;
    }
    if (pos < cap) {
        dst[pos] = '\0';
    }
    return pos;
}

static uint32_t append_num(char* dst, uint32_t cap, uint32_t pos, uint32_t num, int base) {
    char buf[32];
    itoa(num, buf, base);
    return str_append(dst, cap, pos, buf);
}

static uint32_t append_kv_num(char* dst, uint32_t cap, uint32_t pos, const char* key, uint32_t num) {
    pos = str_append(dst, cap, pos, key);
    pos = str_append(dst, cap, pos, ": ");
    pos = append_num(dst, cap, pos, num, 10);
    pos = str_append(dst, cap, pos, "\n");
    return pos;
}

static uint32_t append_kv_hex(char* dst, uint32_t cap, uint32_t pos, const char* key, uint32_t num) {
    pos = str_append(dst, cap, pos, key);
    pos = str_append(dst, cap, pos, ": 0x");
    pos = append_num(dst, cap, pos, num, 16);
    pos = str_append(dst, cap, pos, "\n");
    return pos;
}

static uint32_t append_kv_str(char* dst, uint32_t cap, uint32_t pos, const char* key, const char* val) {
    pos = str_append(dst, cap, pos, key);
    pos = str_append(dst, cap, pos, ": ");
    pos = str_append(dst, cap, pos, val ? val : "");
    pos = str_append(dst, cap, pos, "\n");
    return pos;
}

static int copy_out(const char* src, uint32_t size, uint32_t offset, void* buffer, uint32_t request) {
    if (!src || !buffer) {
        return VFS_ERR_INVALID;
    }
    uint32_t len = size;
    if (offset >= len) {
        return 0;
    }
    uint32_t remaining = len - offset;
    uint32_t to_copy = request;
    if (to_copy > remaining) {
        to_copy = remaining;
    }
    memcpy(buffer, src + offset, to_copy);
    return (int)to_copy;
}

static vnode_t* procfs_make_vnode(const char* name, proc_node_type_t type, pid_t pid, filesystem_t* fs) {
    vnode_t* vnode = (vnode_t*)kmalloc(sizeof(vnode_t));
    if (!vnode) {
        return NULL;
    }

    proc_node_t* node = (proc_node_t*)kmalloc(sizeof(proc_node_t));
    if (!node) {
        kfree(vnode);
        return NULL;
    }

    node->type = type;
    node->pid = pid;
    node->inode = procfs_next_inode++;

    strncpy(vnode->name, name, 255);
    vnode->name[255] = '\0';
    vnode->inode = node->inode;
    vnode->type = (type == PROC_NODE_ROOT || type == PROC_NODE_PID_DIR) ? VFS_DIRECTORY : VFS_FILE;
    vnode->size = 0;
    vnode->flags = 0;
    vnode->refcount = 0;
    vnode->fs = fs;
    vnode->mount = NULL;
    vnode->fs_data = node;
    vnode->ops = &procfs_vnode_ops;

    return vnode;
}

static int procfs_vnode_open(vnode_t* node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static int procfs_vnode_close(vnode_t* node) {
    (void)node;
    return VFS_OK;
}

static int procfs_read_meminfo(void* buffer, uint32_t size, uint32_t offset) {
    char scratch[256];
    uint32_t pos = 0;

    uint32_t total_frames = pmm_get_total_frames();
    uint32_t used_frames = pmm_get_used_frames();
    uint32_t free_frames = pmm_get_free_frames();

    pos = append_kv_num(scratch, sizeof(scratch), pos, "frames_total", total_frames);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "frames_used", used_frames);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "frames_free", free_frames);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "mem_total_kb", total_frames * 4);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "mem_used_kb", used_frames * 4);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "mem_free_kb", free_frames * 4);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "upticks", system_ticks);

    return copy_out(scratch, pos, offset, buffer, size);
}

static int procfs_read_cpuinfo(void* buffer, uint32_t size, uint32_t offset) {
    char scratch[256];
    uint32_t pos = 0;

    uint32_t pit_hz = PIT_BASE_FREQUENCY / PIT_DEFAULT_DIVISOR;
    pos = append_kv_str(scratch, sizeof(scratch), pos, "machine", "aos-core");
    pos = append_kv_str(scratch, sizeof(scratch), pos, "arch", "i386");
    pos = append_kv_num(scratch, sizeof(scratch), pos, "timer_hz", pit_hz);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "tick_counter", system_ticks);

    return copy_out(scratch, pos, offset, buffer, size);
}

static int procfs_read_pidinfo(pid_t pid, void* buffer, uint32_t size, uint32_t offset) {
    process_t* proc = process_get_by_pid(pid);
    if (!proc) {
        return VFS_ERR_NOTFOUND;
    }

    char scratch[256];
    uint32_t pos = 0;

    pos = append_kv_num(scratch, sizeof(scratch), pos, "pid", (uint32_t)proc->pid);
    pos = append_kv_str(scratch, sizeof(scratch), pos, "name", proc->name);
    pos = append_kv_str(scratch, sizeof(scratch), pos, "state", proc_state_name(proc->state));
    pos = append_kv_num(scratch, sizeof(scratch), pos, "priority", (uint32_t)proc->priority);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "time_slice", proc->time_slice);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "total_time", proc->total_time);
    pos = append_kv_num(scratch, sizeof(scratch), pos, "parent", (uint32_t)proc->parent_pid);
    pos = append_kv_hex(scratch, sizeof(scratch), pos, "addr_space", (uint32_t)proc->address_space);
    pos = append_kv_hex(scratch, sizeof(scratch), pos, "kernel_sp", proc->kernel_stack);

    return copy_out(scratch, pos, offset, buffer, size);
}

static int procfs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset) {
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }

    proc_node_t* info = (proc_node_t*)node->fs_data;
    if (!info) {
        return VFS_ERR_INVALID;
    }

    switch (info->type) {
        case PROC_NODE_MEMINFO:
            return procfs_read_meminfo(buffer, size, offset);
        case PROC_NODE_CPUINFO:
            return procfs_read_cpuinfo(buffer, size, offset);
        case PROC_NODE_PID_INFO:
            return procfs_read_pidinfo(info->pid, buffer, size, offset);
        case PROC_NODE_ROOT:
        case PROC_NODE_PID_DIR:
        default:
            return VFS_ERR_ISDIR;
    }
}

static int procfs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset) {
    (void)node;
    (void)buffer;
    (void)size;
    (void)offset;
    return VFS_ERR_PERM;
}

typedef struct procfs_picker {
    uint32_t target;
    uint32_t index;
    process_t* found;
} procfs_picker_t;

static int procfs_pick_cb(process_t* proc, void* user) {
    procfs_picker_t* ctx = (procfs_picker_t*)user;
    if (ctx->index == ctx->target) {
        ctx->found = proc;
        return 1;
    }
    ctx->index++;
    return 0;
}

static int procfs_pick_nth_process(uint32_t target, process_t** out_proc) {
    procfs_picker_t ctx;
    ctx.target = target;
    ctx.index = 0;
    ctx.found = NULL;

    int ret = process_for_each(procfs_pick_cb, &ctx);
    if (ret == 1 && ctx.found) {
        *out_proc = ctx.found;
        return 0;
    }
    return -1;
}

static vnode_t* procfs_vnode_finddir(vnode_t* node, const char* name) {
    proc_node_t* info = (proc_node_t*)node->fs_data;
    if (!info || !name) {
        return NULL;
    }

    if (info->type == PROC_NODE_ROOT) {
        if (strcmp(name, "meminfo") == 0) {
            return procfs_make_vnode("meminfo", PROC_NODE_MEMINFO, 0, node->fs);
        }
        if (strcmp(name, "cpuinfo") == 0) {
            return procfs_make_vnode("cpuinfo", PROC_NODE_CPUINFO, 0, node->fs);
        }

        int pid = atoi(name);
        if (pid > 0 && process_get_by_pid(pid)) {
            return procfs_make_vnode(name, PROC_NODE_PID_DIR, (pid_t)pid, node->fs);
        }
        return NULL;
    }

    if (info->type == PROC_NODE_PID_DIR) {
        if (strcmp(name, "info") == 0) {
            return procfs_make_vnode("info", PROC_NODE_PID_INFO, info->pid, node->fs);
        }
    }

    return NULL;
}

static vnode_t* procfs_vnode_create(vnode_t* parent, const char* name, uint32_t flags) {
    (void)parent;
    (void)name;
    (void)flags;
    return NULL;
}

static int procfs_vnode_unlink(vnode_t* parent, const char* name) {
    (void)parent;
    (void)name;
    return VFS_ERR_PERM;
}

static int procfs_vnode_mkdir(vnode_t* parent, const char* name) {
    (void)parent;
    (void)name;
    return VFS_ERR_PERM;
}

static int procfs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent) {
    proc_node_t* info = (proc_node_t*)node->fs_data;
    if (!info || !dirent) {
        return VFS_ERR_INVALID;
    }

    if (info->type == PROC_NODE_ROOT) {
        if (index == 0) {
            strncpy(dirent->name, "meminfo", 255);
            dirent->name[255] = '\0';
            dirent->inode = 1;
            dirent->type = VFS_FILE;
            return VFS_OK;
        } else if (index == 1) {
            strncpy(dirent->name, "cpuinfo", 255);
            dirent->name[255] = '\0';
            dirent->inode = 2;
            dirent->type = VFS_FILE;
            return VFS_OK;
        } else {
            uint32_t pid_index = index - 2;
            process_t* proc = NULL;
            if (procfs_pick_nth_process(pid_index, &proc) == 0 && proc) {
                char pid_buf[16];
                itoa((uint32_t)proc->pid, pid_buf, 10);
                strncpy(dirent->name, pid_buf, 255);
                dirent->name[255] = '\0';
                dirent->inode = 3 + pid_index;
                dirent->type = VFS_DIRECTORY;
                return VFS_OK;
            }
            return VFS_ERR_NOTFOUND;
        }
    }

    if (info->type == PROC_NODE_PID_DIR) {
        if (index == 0) {
            strncpy(dirent->name, "info", 255);
            dirent->name[255] = '\0';
            dirent->inode = info->inode + 1;
            dirent->type = VFS_FILE;
            return VFS_OK;
        }
        return VFS_ERR_NOTFOUND;
    }

    return VFS_ERR_NOTFOUND;
}

static int procfs_vnode_stat(vnode_t* node, stat_t* stat) {
    proc_node_t* info = (proc_node_t*)node->fs_data;
    if (!info || !stat) {
        return VFS_ERR_INVALID;
    }

    stat->st_dev = 0;
    stat->st_ino = info->inode;
    stat->st_mode = (info->type == PROC_NODE_ROOT || info->type == PROC_NODE_PID_DIR) ? VFS_DIRECTORY : VFS_FILE;
    stat->st_nlink = 1;
    stat->st_uid = 0;
    stat->st_gid = 0;
    stat->st_rdev = 0;
    stat->st_size = 0;
    stat->st_blksize = 512;
    stat->st_blocks = 0;

    return VFS_OK;
}

static int procfs_mount(filesystem_t* fs, const char* source, uint32_t flags) {
    (void)source;
    (void)flags;

    procfs_root_node.type = PROC_NODE_ROOT;
    procfs_root_node.pid = 0;
    procfs_root_node.inode = 0;
    fs->fs_data = &procfs_root_node;

    return VFS_OK;
}

static int procfs_unmount(filesystem_t* fs) {
    (void)fs;
    return VFS_OK;
}

static vnode_t* procfs_get_root(filesystem_t* fs) {
    return procfs_make_vnode("/", PROC_NODE_ROOT, 0, fs);
}

void procfs_init(void) {
    serial_puts("Initializing procfs...\n");
    vfs_register_filesystem(&procfs_fs);
}

filesystem_t* procfs_get_fs(void) {
    return &procfs_fs;
}
