/*
 * === AOS HEADER BEGIN ===
 * src/fs/simplefs.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


// SimpleFS: A modern, complete block-based filesystem designed specifically for aOS
// Inspired by ext2 with modern enhancements
// Block size: 512 bytes (matches ATA sector size)
// Max file size: ~258MB (12 direct + 1-level indirect + 2-level indirect)
// Max files: Limited by inode count
// Note: This filesystem may seem absurd, but it is what i deemed best.

#include <fs/simplefs.h>
#include <fs/vfs.h>
#include <dev/ata.h>

#include <string.h>
#include <stdlib.h>
#include <fileperm.h>
#include <process.h>

#include <serial.h>
#include <vmm.h>

#include <util.h>


// Forward declarations
static int simplefs_vnode_open(vnode_t* node, uint32_t flags);
static int simplefs_vnode_close(vnode_t* node);
static int simplefs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset);
static int simplefs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset);
static vnode_t* simplefs_vnode_finddir(vnode_t* node, const char* name);
static vnode_t* simplefs_vnode_create(vnode_t* parent, const char* name, uint32_t flags);
static int simplefs_vnode_unlink(vnode_t* parent, const char* name);
static int simplefs_vnode_mkdir(vnode_t* parent, const char* name);
static int simplefs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent);
static int simplefs_vnode_stat(vnode_t* node, stat_t* stat);

// Helper function forward declarations
static int write_inode(simplefs_data_t* fs_data, uint32_t inode_num);
static int read_block(simplefs_data_t* fs_data, uint32_t block_num, void* buffer);
static int write_block(simplefs_data_t* fs_data, uint32_t block_num, const void* buffer);
static uint32_t alloc_block(simplefs_data_t* fs_data);
static uint32_t alloc_inode(simplefs_data_t* fs_data);
static void free_inode(simplefs_data_t* fs_data, uint32_t inode_num);
static void free_block_num(simplefs_data_t* fs_data, uint32_t block_num);
static void free_inode_blocks(simplefs_data_t* fs_data, uint32_t inode_num);
static vnode_t* simplefs_inode_to_vnode(simplefs_data_t* fs_data, uint32_t inode_num, const char* name, filesystem_t* fs);
static uint32_t get_file_block(simplefs_data_t* fs_data, simplefs_inode_t* inode, uint32_t block_idx);
static int set_file_block(simplefs_data_t* fs_data, simplefs_inode_t* inode, uint32_t block_idx, uint32_t block_num, uint32_t inode_num);
static uint32_t get_current_time(void);
static uint32_t calculate_checksum(const uint8_t* data, uint32_t size);

// VFS operations
static vnode_ops_t simplefs_vnode_ops = {
    .open = simplefs_vnode_open,
    .close = simplefs_vnode_close,
    .read = simplefs_vnode_read,
    .write = simplefs_vnode_write,
    .finddir = simplefs_vnode_finddir,
    .create = simplefs_vnode_create,
    .unlink = simplefs_vnode_unlink,
    .mkdir = simplefs_vnode_mkdir,
    .readdir = simplefs_vnode_readdir,
    .stat = simplefs_vnode_stat
};

// Filesystem operations
static int simplefs_mount(filesystem_t* fs, const char* source, uint32_t flags);
static int simplefs_unmount(filesystem_t* fs);
static vnode_t* simplefs_get_root(filesystem_t* fs);

static filesystem_ops_t simplefs_fs_ops = {
    .mount = simplefs_mount,
    .unmount = simplefs_unmount,
    .get_root = simplefs_get_root
};

static filesystem_t simplefs_filesystem = {
    .name = "simplefs",
    .ops = &simplefs_fs_ops,
    .fs_data = NULL,
    .mount = NULL
};

// Helper functions

// Get current time (placeholder - returns 0 until we have a real-time clock)
static uint32_t get_current_time(void) {
    // TODO: Implement RTC driver to get actual time
    // For now, return a monotonic counter or 0
    static uint32_t counter = 0;
    return counter++;
}

// Calculate simple checksum for data
static uint32_t calculate_checksum(const uint8_t* data, uint32_t size) {
    uint32_t checksum = 0;
    for (uint32_t i = 0; i < size; i++) {
        checksum += data[i];
        checksum = (checksum << 1) | (checksum >> 31); // Rotate left
    }
    return checksum;
}

static int read_block(simplefs_data_t* fs_data, uint32_t block_num, void* buffer) {
    if (!fs_data || !buffer) {
        serial_puts("SimpleFS: read_block - null pointer\n");
        return -1;
    }
    
    // Validate block_num after superblock is initialized
    if (fs_data->superblock.magic == SIMPLEFS_MAGIC && block_num >= fs_data->superblock.total_blocks) {
        serial_puts("SimpleFS: read_block - block out of bounds\n");
        return -1;
    }
    
    // Note: We can't check total_blocks on first superblock read since it's not initialized yet
    // The caller must ensure block_num is valid
    uint32_t lba = fs_data->start_lba + block_num;
    return ata_read_sectors(lba, 1, (uint8_t*)buffer);
}

static int write_block(simplefs_data_t* fs_data, uint32_t block_num, const void* buffer) {
    if (!fs_data || !buffer) {
        serial_puts("SimpleFS: write_block - null pointer\n");
        return -1;
    }
    
    // Validate block_num after superblock is initialized
    if (fs_data->superblock.magic == SIMPLEFS_MAGIC && block_num >= fs_data->superblock.total_blocks) {
        serial_puts("SimpleFS: write_block - block out of bounds\n");
        return -1;
    }
    
    // Note: We can't check total_blocks on first superblock write since it's not initialized yet
    // The caller must ensure block_num is valid
    uint32_t lba = fs_data->start_lba + block_num;
    int result = ata_write_sectors(lba, 1, (const uint8_t*)buffer);
    if (result != 0) {
        serial_puts("SimpleFS: write_block failed for block ");
        char buf[32];
        itoa(block_num, buf, 10);
        serial_puts(buf);
        serial_puts(" (LBA ");
        itoa(lba, buf, 10);
        serial_puts(buf);
        serial_puts(")\n");
    }
    return result;
}

static uint32_t alloc_block(simplefs_data_t* fs_data) {
    if (!fs_data || !fs_data->block_bitmap) {
        serial_puts("SimpleFS: alloc_block - null pointer\n");
        return 0;
    }
    
    // Check if we have free blocks
    if (fs_data->superblock.free_blocks == 0) {
        serial_puts("SimpleFS: No free blocks available\n");
        return 0;
    }
    
    // Find free block in bitmap
    for (uint32_t i = fs_data->superblock.first_data_block; i < fs_data->superblock.total_blocks; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(fs_data->block_bitmap[byte_idx] & (1 << bit_idx))) {
            // Mark as allocated
            fs_data->block_bitmap[byte_idx] |= (1 << bit_idx);
            fs_data->superblock.free_blocks--;
            
            // Write back bitmap
            write_block(fs_data, fs_data->superblock.block_bitmap_block, fs_data->block_bitmap);
            
            // Write back superblock
            write_block(fs_data, 0, &fs_data->superblock);
            
            // Zero out the new block
            uint8_t zero_block[SIMPLEFS_BLOCK_SIZE];
            memset(zero_block, 0, SIMPLEFS_BLOCK_SIZE);
            write_block(fs_data, i, zero_block);
            
            return i;
        }
    }
    return 0; // No free blocks
}

// Mark a block as free (currently unused but kept for future file deletion support)
static void free_block(simplefs_data_t* fs_data, uint32_t block_num) __attribute__((unused));
static void free_block(simplefs_data_t* fs_data, uint32_t block_num) {
    if (!fs_data || !fs_data->block_bitmap) {
        serial_puts("SimpleFS: free_block - null pointer\n");
        return;
    }
    
    if (block_num < fs_data->superblock.first_data_block || block_num >= fs_data->superblock.total_blocks) {
        serial_puts("SimpleFS: free_block - invalid block number\n");
        return;
    }
    
    uint32_t byte_idx = block_num / 8;
    uint32_t bit_idx = block_num % 8;
    
    fs_data->block_bitmap[byte_idx] &= ~(1 << bit_idx);
    fs_data->superblock.free_blocks++;
    
    // Write back bitmap and superblock
    write_block(fs_data, fs_data->superblock.block_bitmap_block, fs_data->block_bitmap);
    write_block(fs_data, 0, &fs_data->superblock);
}

// Get the physical block number for a file's logical block index
// Handles direct, indirect, and double indirect blocks
static uint32_t get_file_block(simplefs_data_t* fs_data, simplefs_inode_t* inode, uint32_t block_idx) {
    if (!fs_data || !inode) {
        serial_puts("SimpleFS: get_file_block - null pointer\n");
        return 0;
    }
    
    // Direct blocks
    if (block_idx < SIMPLEFS_DIRECT_BLOCKS) {
        uint32_t block_num = inode->block_ptrs[block_idx];
        // Validate block number
        if (block_num > 0 && block_num >= fs_data->superblock.total_blocks) {
            serial_puts("SimpleFS: Invalid direct block pointer\n");
            return 0;
        }
        return block_num;
    }
    
    block_idx -= SIMPLEFS_DIRECT_BLOCKS;
    uint32_t ptrs_per_block = SIMPLEFS_BLOCK_SIZE / sizeof(uint32_t);
    
    // Single indirect block
    if (block_idx < ptrs_per_block) {
        if (inode->indirect_ptr == 0) {
            return 0;
        }
        
        uint32_t indirect_block[ptrs_per_block];
        if (read_block(fs_data, inode->indirect_ptr, indirect_block) != 0) {
            return 0;
        }
        
        return indirect_block[block_idx];
    }
    
    block_idx -= ptrs_per_block;
    
    // Double indirect block
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode->double_indirect_ptr == 0) {
            return 0;
        }
        
        uint32_t l1_idx = block_idx / ptrs_per_block;
        uint32_t l2_idx = block_idx % ptrs_per_block;
        
        uint32_t l1_block[ptrs_per_block];
        if (read_block(fs_data, inode->double_indirect_ptr, l1_block) != 0) {
            return 0;
        }
        
        if (l1_block[l1_idx] == 0) {
            return 0;
        }
        
        uint32_t l2_block[ptrs_per_block];
        if (read_block(fs_data, l1_block[l1_idx], l2_block) != 0) {
            return 0;
        }
        
        return l2_block[l2_idx];
    }
    
    // Beyond double indirect (not supported yet)
    return 0;
}

// Set the physical block number for a file's logical block index
// Allocates indirect blocks as needed
static int set_file_block(simplefs_data_t* fs_data, simplefs_inode_t* inode, uint32_t block_idx, uint32_t block_num, uint32_t inode_num) {
    // Direct blocks
    if (block_idx < SIMPLEFS_DIRECT_BLOCKS) {
        inode->block_ptrs[block_idx] = block_num;
        write_inode(fs_data, inode_num);
        return 0;
    }
    
    block_idx -= SIMPLEFS_DIRECT_BLOCKS;
    uint32_t ptrs_per_block = SIMPLEFS_BLOCK_SIZE / sizeof(uint32_t);
    
    // Single indirect block
    if (block_idx < ptrs_per_block) {
        // Allocate indirect block if needed
        if (inode->indirect_ptr == 0) {
            inode->indirect_ptr = alloc_block(fs_data);
            if (inode->indirect_ptr == 0) {
                return -1;
            }
            write_inode(fs_data, inode_num);
        }
        
        uint32_t indirect_block[ptrs_per_block];
        if (read_block(fs_data, inode->indirect_ptr, indirect_block) != 0) {
            return -1;
        }
        
        indirect_block[block_idx] = block_num;
        
        return write_block(fs_data, inode->indirect_ptr, indirect_block);
    }
    
    block_idx -= ptrs_per_block;
    
    // Double indirect block
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        // Allocate double indirect block if needed
        if (inode->double_indirect_ptr == 0) {
            inode->double_indirect_ptr = alloc_block(fs_data);
            if (inode->double_indirect_ptr == 0) {
                return -1;
            }
            write_inode(fs_data, inode_num);
        }
        
        uint32_t l1_idx = block_idx / ptrs_per_block;
        uint32_t l2_idx = block_idx % ptrs_per_block;
        
        uint32_t l1_block[ptrs_per_block];
        if (read_block(fs_data, inode->double_indirect_ptr, l1_block) != 0) {
            return -1;
        }
        
        // Allocate level 2 indirect block if needed
        if (l1_block[l1_idx] == 0) {
            l1_block[l1_idx] = alloc_block(fs_data);
            if (l1_block[l1_idx] == 0) {
                return -1;
            }
            write_block(fs_data, inode->double_indirect_ptr, l1_block);
        }
        
        uint32_t l2_block[ptrs_per_block];
        if (read_block(fs_data, l1_block[l1_idx], l2_block) != 0) {
            return -1;
        }
        
        l2_block[l2_idx] = block_num;
        
        return write_block(fs_data, l1_block[l1_idx], l2_block);
    }
    
    // Beyond double indirect (not supported yet)
    return -1;
}

static uint32_t alloc_inode(simplefs_data_t* fs_data) {
    serial_puts("alloc_inode: searching for free inode...\n");
    char buf[32];
    serial_puts("alloc_inode: total_inodes=");
    itoa(fs_data->superblock.total_inodes, buf, 10);
    serial_puts(buf);
    serial_puts(", free_inodes=");
    itoa(fs_data->superblock.free_inodes, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    // Find free inode in bitmap
    for (uint32_t i = 1; i < fs_data->superblock.total_inodes; i++) { // Start from 1, 0 is reserved
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;
        
        if (!(fs_data->inode_bitmap[byte_idx] & (1 << bit_idx))) {
            // Found free inode
            serial_puts("alloc_inode: found free inode ");
            itoa(i, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
            
            // Mark as allocated
            fs_data->inode_bitmap[byte_idx] |= (1 << bit_idx);
            fs_data->superblock.free_inodes--;
            
            // Write back bitmap
            write_block(fs_data, fs_data->superblock.inode_bitmap_block, fs_data->inode_bitmap);
            
            // Write back superblock
            write_block(fs_data, 0, &fs_data->superblock);
            
            // Initialize inode
            simplefs_inode_t* inode = &fs_data->inode_table[i];
            memset(inode, 0, sizeof(simplefs_inode_t));
            
            return i;
        }
    }
    
    serial_puts("alloc_inode: NO FREE INODES FOUND!\n");
    return 0; // No free inodes
}

static void free_block_num(simplefs_data_t* fs_data, uint32_t block_num) {
    if (block_num == 0 || block_num >= fs_data->superblock.total_blocks) {
        return;
    }
    
    uint32_t byte_idx = block_num / 8;
    uint32_t bit_idx = block_num % 8;
    
    // Check if block is already free
    if (!(fs_data->block_bitmap[byte_idx] & (1 << bit_idx))) {
        return; // Already free
    }
    
    fs_data->block_bitmap[byte_idx] &= ~(1 << bit_idx);
    fs_data->superblock.free_blocks++;
    
    // Write back bitmap and superblock
    write_block(fs_data, fs_data->superblock.block_bitmap_block, fs_data->block_bitmap);
    write_block(fs_data, 0, &fs_data->superblock);
}

static void free_inode(simplefs_data_t* fs_data, uint32_t inode_num) {
    if (inode_num == 0 || inode_num >= fs_data->superblock.total_inodes) {
        return;
    }
    
    uint32_t byte_idx = inode_num / 8;
    uint32_t bit_idx = inode_num % 8;
    
    fs_data->inode_bitmap[byte_idx] &= ~(1 << bit_idx);
    fs_data->superblock.free_inodes++;
    
    // Write back bitmap and superblock
    write_block(fs_data, fs_data->superblock.inode_bitmap_block, fs_data->inode_bitmap);
    write_block(fs_data, 0, &fs_data->superblock);
}

static void free_inode_blocks(simplefs_data_t* fs_data, uint32_t inode_num) {
    if (inode_num >= fs_data->superblock.total_inodes) {
        return;
    }
    
    simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
    uint32_t ptrs_per_block = SIMPLEFS_BLOCK_SIZE / sizeof(uint32_t);
    
    // Free all direct blocks
    for (uint32_t i = 0; i < SIMPLEFS_DIRECT_BLOCKS; i++) {
        if (inode->block_ptrs[i] != 0) {
            free_block_num(fs_data, inode->block_ptrs[i]);
            inode->block_ptrs[i] = 0;
        }
    }
    
    // Free indirect block and its data blocks
    if (inode->indirect_ptr != 0) {
        uint32_t indirect_block[ptrs_per_block];
        if (read_block(fs_data, inode->indirect_ptr, indirect_block) == 0) {
            for (uint32_t i = 0; i < ptrs_per_block; i++) {
                if (indirect_block[i] != 0) {
                    free_block_num(fs_data, indirect_block[i]);
                }
            }
        }
        free_block_num(fs_data, inode->indirect_ptr);
        inode->indirect_ptr = 0;
    }
    
    // Free double indirect block and its data blocks
    if (inode->double_indirect_ptr != 0) {
        uint32_t l1_block[ptrs_per_block];
        if (read_block(fs_data, inode->double_indirect_ptr, l1_block) == 0) {
            for (uint32_t i = 0; i < ptrs_per_block; i++) {
                if (l1_block[i] != 0) {
                    uint32_t l2_block[ptrs_per_block];
                    if (read_block(fs_data, l1_block[i], l2_block) == 0) {
                        for (uint32_t j = 0; j < ptrs_per_block; j++) {
                            if (l2_block[j] != 0) {
                                free_block_num(fs_data, l2_block[j]);
                            }
                        }
                    }
                    free_block_num(fs_data, l1_block[i]);
                }
            }
        }
        free_block_num(fs_data, inode->double_indirect_ptr);
        inode->double_indirect_ptr = 0;
    }
    
    inode->blocks = 0;
    inode->size = 0;
    write_inode(fs_data, inode_num);
}

static int write_inode(simplefs_data_t* fs_data, uint32_t inode_num) {
    if (inode_num >= fs_data->superblock.total_inodes) {
        return -1;
    }
    
    // Calculate which block contains this inode
    uint32_t inodes_per_block = SIMPLEFS_BLOCK_SIZE / sizeof(simplefs_inode_t);
    uint32_t block_num = fs_data->superblock.inode_table_block + (inode_num / inodes_per_block);
    uint32_t offset = (inode_num % inodes_per_block) * sizeof(simplefs_inode_t);
    
    // Read block, modify inode, write back
    uint8_t block[SIMPLEFS_BLOCK_SIZE];
    if (read_block(fs_data, block_num, block) != 0) {
        return -1;
    }
    
    memcpy(block + offset, &fs_data->inode_table[inode_num], sizeof(simplefs_inode_t));
    
    return write_block(fs_data, block_num, block);
}

static vnode_t* simplefs_inode_to_vnode(simplefs_data_t* fs_data, uint32_t inode_num, const char* name, filesystem_t* fs) {
    if (inode_num >= fs_data->superblock.total_inodes) {
        return NULL;
    }
    
    simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
    
   // char buf[16];
   // serial_puts("SimpleFS: inode_to_vnode - inode ");
   // itoa(inode_num, buf, 10);
   // serial_puts(buf);
   // serial_puts(", mode=");
    //itoa(inode->mode, buf, 16);
    //serial_puts(buf);
   // serial_puts("\n");
    
    vnode_t* vnode = (vnode_t*)kmalloc(sizeof(vnode_t));
    if (!vnode) {
        return NULL;
    }
    
    strncpy(vnode->name, name, 255);
    vnode->name[255] = '\0';
    vnode->inode = inode_num;
    
    // Convert SimpleFS mode to VFS type
    if (SIMPLEFS_ISDIR(inode->mode)) {
        vnode->type = VFS_DIRECTORY;
    } else if (SIMPLEFS_ISLNK(inode->mode)) {
        vnode->type = VFS_SYMLINK;
    } else if (SIMPLEFS_ISREG(inode->mode)) {
        vnode->type = VFS_FILE;
    } else if (SIMPLEFS_ISCHR(inode->mode)) {
        vnode->type = VFS_CHARDEV;
    } else if (SIMPLEFS_ISBLK(inode->mode)) {
        vnode->type = VFS_BLOCKDEV;
    } else {
        vnode->type = VFS_FILE; // Default to file
    }
    
    vnode->size = inode->size | ((uint64_t)inode->size_high << 32);
    vnode->flags = 0;
    vnode->refcount = 0;
    vnode->fs = fs;
    vnode->mount = NULL;
    vnode->fs_data = (void*)(uint32_t)inode_num; // Store inode number
    vnode->ops = &simplefs_vnode_ops;
    
    // Load permissions from inode (v0.7.3)
    // If owner_type is 0 and uid is 0, it's a legacy inode - treat as SYSTEM
    if (inode->owner_type == 0 && inode->uid == 0) {
        // Legacy or system file
        if (vnode->type == VFS_DIRECTORY) {
            vnode->access = fileperm_default_dir(0, OWNER_SYSTEM);
        } else {
            vnode->access = fileperm_default_file(0, OWNER_SYSTEM);
        }
    } else {
        // Load stored ownership from inode
        owner_type_t owner_type = (owner_type_t)inode->owner_type;
        if (vnode->type == VFS_DIRECTORY) {
            vnode->access = fileperm_default_dir(inode->uid, owner_type);
        } else {
            vnode->access = fileperm_default_file(inode->uid, owner_type);
        }
    }
    
    return vnode;
}

// Filesystem operations implementation

static int simplefs_mount(filesystem_t* fs, const char* source, uint32_t flags) {
    (void)source;
    (void)flags;
    
    serial_puts("SimpleFS: Mounting filesystem...\n");
    
    if (!ata_drive_available()) {
        serial_puts("SimpleFS: No ATA drive available\n");
        return VFS_ERR_IO;
    }
    
    // Allocate filesystem data
    simplefs_data_t* fs_data = (simplefs_data_t*)kmalloc(sizeof(simplefs_data_t));
    if (!fs_data) {
        return VFS_ERR_NOSPACE;
    }
    
    memset(fs_data, 0, sizeof(simplefs_data_t));
    fs_data->start_lba = 0; // Start at LBA 0 for now
    
    // Read superblock
    if (read_block(fs_data, 0, &fs_data->superblock) != 0) {
        serial_puts("SimpleFS: Failed to read superblock\n");
        kfree(fs_data);
        return VFS_ERR_IO;
    }
    
    // Verify magic number
    if (fs_data->superblock.magic != SIMPLEFS_MAGIC) {
        serial_puts("SimpleFS: Invalid magic number\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }
    
    // Verify version
    if (fs_data->superblock.version != SIMPLEFS_VERSION) {
        serial_puts("SimpleFS: Unsupported version\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }
    
    serial_puts("SimpleFS: Valid superblock found (version ");
    char buf[16];
    itoa(fs_data->superblock.version, buf, 10);
    serial_puts(buf);
    serial_puts(")\n");
    
    // Update mount information
    fs_data->superblock.mount_count++;
    fs_data->superblock.last_mount_time = get_current_time();
    
    // Check if journal recovery needed
    if (fs_data->superblock.state != SIMPLEFS_JOURNAL_CLEAN) {
        serial_puts("SimpleFS: Filesystem not clean, recovery needed\n");
        fs_data->superblock.state = SIMPLEFS_JOURNAL_RECOVERING;
        write_block(fs_data, 0, &fs_data->superblock);
        
        // TODO: Implement journal recovery
        // simplefs_journal_recover(fs_data);
        
        fs_data->superblock.state = SIMPLEFS_JOURNAL_CLEAN;
    }
    
    write_block(fs_data, 0, &fs_data->superblock);
    
    // Allocate and read bitmaps
    fs_data->block_bitmap = (uint8_t*)kmalloc(SIMPLEFS_BLOCK_SIZE);
    fs_data->inode_bitmap = (uint8_t*)kmalloc(SIMPLEFS_BLOCK_SIZE);
    
    if (!fs_data->block_bitmap || !fs_data->inode_bitmap) {
        if (fs_data->block_bitmap) kfree(fs_data->block_bitmap);
        if (fs_data->inode_bitmap) kfree(fs_data->inode_bitmap);
        kfree(fs_data);
        return VFS_ERR_NOSPACE;
    }
    
    read_block(fs_data, fs_data->superblock.block_bitmap_block, fs_data->block_bitmap);
    read_block(fs_data, fs_data->superblock.inode_bitmap_block, fs_data->inode_bitmap);
    
    // Allocate and read inode table
    uint32_t inode_table_size = fs_data->superblock.total_inodes * sizeof(simplefs_inode_t);
    fs_data->inode_table = (simplefs_inode_t*)kmalloc(inode_table_size);
    if (!fs_data->inode_table) {
        kfree(fs_data->block_bitmap);
        kfree(fs_data->inode_bitmap);
        kfree(fs_data);
        return VFS_ERR_NOSPACE;
    }
    
    // Read inode table blocks
    uint32_t inodes_per_block = SIMPLEFS_BLOCK_SIZE / sizeof(simplefs_inode_t);
    uint32_t inode_blocks = (fs_data->superblock.total_inodes + inodes_per_block - 1) / inodes_per_block;
    
    serial_puts("SimpleFS: Reading inode table, inodes_per_block=");
    char tmp_buf[16];
    itoa(inodes_per_block, tmp_buf, 10);
    serial_puts(tmp_buf);
    serial_puts(", inode_blocks=");
    itoa(inode_blocks, tmp_buf, 10);
    serial_puts(tmp_buf);
    serial_puts(", sizeof(simplefs_inode_t)=");
    itoa(sizeof(simplefs_inode_t), tmp_buf, 10);
    serial_puts(tmp_buf);
    serial_puts("\n");
    
    // Read all inode blocks
    // Note: Unwritten blocks will return zeros from disk (standard behavior)
    for (uint32_t i = 0; i < inode_blocks; i++) {
        uint8_t block[SIMPLEFS_BLOCK_SIZE];
        int read_result = read_block(fs_data, fs_data->superblock.inode_table_block + i, block);
        
        if (read_result != 0 && i == 0) {
            // Only fail if we can't read the critical first block
            serial_puts("SimpleFS: Failed to read first inode block\n");
            return VFS_ERR_IO;
        }
        
        uint32_t inodes_in_block = inodes_per_block;
        if (i == inode_blocks - 1) {
            inodes_in_block = fs_data->superblock.total_inodes - (i * inodes_per_block);
        }
        
        memcpy(&fs_data->inode_table[i * inodes_per_block], block, inodes_in_block * sizeof(simplefs_inode_t));
        
        // Debug: Show first block's root inode data
        if (i == 0) {
            simplefs_inode_t* inode_0 = (simplefs_inode_t*)block;
            serial_puts("SimpleFS: Loaded inode[0].mode=");
            itoa(inode_0[0].mode, tmp_buf, 16);
            serial_puts(tmp_buf);
            serial_puts(", inode[1].mode=");
            itoa(inode_0[1].mode, tmp_buf, 16);
            serial_puts(tmp_buf);
            serial_puts("\n");
        }
    }
    
    // Initialize journal data
    fs_data->journal_seq = 0;
    fs_data->journal_enabled = (fs_data->superblock.journal_size > 0) ? 1 : 0;
    fs_data->dirty_count = 0;
    
    fs->fs_data = fs_data;
    
    serial_puts("SimpleFS mounted successfully.\n");
    return VFS_OK;
}

static int simplefs_unmount(filesystem_t* fs) {
    if (!fs || !fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)fs->fs_data;
    
    // Mark filesystem as clean before unmount
    fs_data->superblock.state = SIMPLEFS_JOURNAL_CLEAN;
    fs_data->superblock.last_write_time = get_current_time();
    write_block(fs_data, 0, &fs_data->superblock);
    
    // Free allocated memory
    if (fs_data->block_bitmap) kfree(fs_data->block_bitmap);
    if (fs_data->inode_bitmap) kfree(fs_data->inode_bitmap);
    if (fs_data->inode_table) kfree(fs_data->inode_table);
    kfree(fs_data);
    
    fs->fs_data = NULL;
    
    return VFS_OK;
}

static vnode_t* simplefs_get_root(filesystem_t* fs) {
    if (!fs || !fs->fs_data) {
        return NULL;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)fs->fs_data;
    
    // Validate root inode before returning it
    simplefs_inode_t* root_inode = &fs_data->inode_table[1];
    
    // Check if root inode is valid (should be a directory)
    if (root_inode->mode == 0 || !SIMPLEFS_ISDIR(root_inode->mode)) {
        serial_puts("SimpleFS: Root inode is corrupted (mode=");
        char buf[16];
        itoa(root_inode->mode, buf, 16);
        serial_puts(buf);
        serial_puts("), filesystem is invalid\n");
        return NULL;
    }
    
    // Root directory is always inode 1
    return simplefs_inode_to_vnode(fs_data, 1, "/", fs);
}

// Vnode operations implementation

static int simplefs_vnode_open(vnode_t* node, uint32_t flags) {
    (void)node;
    (void)flags;
    return VFS_OK;
}

static int simplefs_vnode_close(vnode_t* node) {
    (void)node;
    return VFS_OK;
}

static int simplefs_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset) {
    if (!node || !buffer) {
        return VFS_ERR_INVALID;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)node->fs->fs_data;
    uint32_t inode_num = (uint32_t)node->fs_data;
    simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
    
    // Check if offset is beyond file size
    if (offset >= inode->size) {
        return 0;
    }
    
    // Calculate bytes to read
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > inode->size) {
        bytes_to_read = inode->size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint8_t block_buffer[SIMPLEFS_BLOCK_SIZE];
    
    while (bytes_read < bytes_to_read) {
        uint32_t block_idx = (offset + bytes_read) / SIMPLEFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_read) % SIMPLEFS_BLOCK_SIZE;
        uint32_t bytes_in_block = SIMPLEFS_BLOCK_SIZE - block_offset;
        
        if (bytes_in_block > bytes_to_read - bytes_read) {
            bytes_in_block = bytes_to_read - bytes_read;
        }
        
        // Use new get_file_block function that handles indirect blocks
        uint32_t block_num = get_file_block(fs_data, inode, block_idx);
        
        if (block_num == 0) {
            // Sparse file - return zeros
            memset((uint8_t*)buffer + bytes_read, 0, bytes_in_block);
        } else {
            if (read_block(fs_data, block_num, block_buffer) != 0) {
                return bytes_read > 0 ? (int32_t)bytes_read : VFS_ERR_IO;
            }
            memcpy((uint8_t*)buffer + bytes_read, block_buffer + block_offset, bytes_in_block);
        }
        
        bytes_read += bytes_in_block;
    }
    
    // Update access time
    inode->atime = get_current_time();
    write_inode(fs_data, inode_num);
    
    return bytes_read;
}

static int simplefs_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset) {
    if (!node || !buffer) {
        serial_puts("SimpleFS: write failed - null node or buffer\n");
        return VFS_ERR_INVALID;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)node->fs->fs_data;
    uint32_t inode_num = (uint32_t)node->fs_data;
    simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
    
    serial_puts("SimpleFS: Writing ");
    char buf[32];
    itoa(size, buf, 10);
    serial_puts(buf);
    serial_puts(" bytes at offset ");
    itoa(offset, buf, 10);
    serial_puts(buf);
    serial_puts(" to inode ");
    itoa(inode_num, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    uint32_t bytes_written = 0;
    uint8_t block_buffer[SIMPLEFS_BLOCK_SIZE];
    
    while (bytes_written < size) {
        uint32_t block_idx = (offset + bytes_written) / SIMPLEFS_BLOCK_SIZE;
        uint32_t block_offset = (offset + bytes_written) % SIMPLEFS_BLOCK_SIZE;
        uint32_t bytes_in_block = SIMPLEFS_BLOCK_SIZE - block_offset;
        
        if (bytes_in_block > size - bytes_written) {
            bytes_in_block = size - bytes_written;
        }
        
        // Get existing block number or 0 if not allocated
        uint32_t block_num = get_file_block(fs_data, inode, block_idx);
        
        // Allocate block if necessary
        if (block_num == 0) {
            serial_puts("SimpleFS: Allocating new block for index ");
            itoa(block_idx, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
            
            uint32_t new_block = alloc_block(fs_data);
            if (new_block == 0) {
                serial_puts("SimpleFS: Failed to allocate block - no space\n");
                return bytes_written > 0 ? (int32_t)bytes_written : VFS_ERR_NOSPACE;
            }
            
            // Use new set_file_block function that handles indirect blocks
            if (set_file_block(fs_data, inode, block_idx, new_block, inode_num) != 0) {
                serial_puts("SimpleFS: Failed to set file block\n");
                free_block_num(fs_data, new_block);
                return bytes_written > 0 ? (int32_t)bytes_written : VFS_ERR_IO;
            }
            
            block_num = new_block;
            inode->blocks++;
            
            serial_puts("SimpleFS: Allocated block ");
            itoa(new_block, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
        }
        
        // Read-modify-write for partial blocks
        if (block_offset != 0 || bytes_in_block != SIMPLEFS_BLOCK_SIZE) {
            serial_puts("SimpleFS: Reading block ");
            itoa(block_num, buf, 10);
            serial_puts(buf);
            serial_puts(" for partial write\n");
            
            if (read_block(fs_data, block_num, block_buffer) != 0) {
                serial_puts("SimpleFS: Failed to read block for partial write\n");
                return bytes_written > 0 ? (int32_t)bytes_written : VFS_ERR_IO;
            }
        }
        
        memcpy(block_buffer + block_offset, (const uint8_t*)buffer + bytes_written, bytes_in_block);
        
        serial_puts("SimpleFS: Writing block ");
        itoa(block_num, buf, 10);
        serial_puts(buf);
        serial_puts("\n");
        
        if (write_block(fs_data, block_num, block_buffer) != 0) {
            serial_puts("SimpleFS: Failed to write block\n");
            return bytes_written > 0 ? (int32_t)bytes_written : VFS_ERR_IO;
        }
        
        bytes_written += bytes_in_block;
    }
    
    // Update size if we wrote past the end
    if (offset + bytes_written > inode->size) {
        inode->size = offset + bytes_written;
        inode->size_high = ((uint64_t)(offset + bytes_written)) >> 32;
        node->size = offset + bytes_written;
    }
    
    // Update modification time
    inode->mtime = get_current_time();
    
    // Write back inode
    write_inode(fs_data, inode_num);
    
    return bytes_written;
}

static vnode_t* simplefs_vnode_finddir(vnode_t* node, const char* name) {
    if (!node || !name || node->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)node->fs->fs_data;
    uint32_t inode_num = (uint32_t)node->fs_data;
    simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
    
    uint8_t block_buffer[SIMPLEFS_BLOCK_SIZE];
    
    // Search through directory entries
    for (uint32_t block_idx = 0; block_idx < SIMPLEFS_DIRECT_BLOCKS && inode->block_ptrs[block_idx] != 0; block_idx++) {
        if (read_block(fs_data, inode->block_ptrs[block_idx], block_buffer) != 0) {
            continue;
        }
        
        uint32_t offset = 0;
        while (offset < SIMPLEFS_BLOCK_SIZE) {
            simplefs_dirent_t* dirent = (simplefs_dirent_t*)(block_buffer + offset);
            
            if (dirent->inode == 0 || dirent->rec_len == 0) {
                break;
            }
            
            if (dirent->name_len > 0 && strcmp(dirent->name, name) == 0) {
                return simplefs_inode_to_vnode(fs_data, dirent->inode, dirent->name, node->fs);
            }
            
            offset += dirent->rec_len;
        }
    }
    
    return NULL;
}

static vnode_t* simplefs_vnode_create(vnode_t* parent, const char* name, uint32_t flags) {
    if (!parent || !name || parent->type != VFS_DIRECTORY) {
        serial_puts("SimpleFS: create failed - invalid parent or name or not a directory\n");
        return NULL;
    }
    
    (void)flags; // TODO: Use flags for permissions
    
    serial_puts("SimpleFS: Creating file '");
    serial_puts(name);
    serial_puts("'\n");
    
    simplefs_data_t* fs_data = (simplefs_data_t*)parent->fs->fs_data;
    
    // Allocate new inode
    serial_puts("SimpleFS: Allocating inode...\n");
    uint32_t new_inode_num = alloc_inode(fs_data);
    if (new_inode_num == 0) {
        serial_puts("SimpleFS: Failed to allocate inode\n");
        return NULL;
    }
    
    char buf[16];
    itoa(new_inode_num, buf, 10);
    serial_puts("SimpleFS: Allocated inode ");
    serial_puts(buf);
    serial_puts("\n");
    
    // Initialize inode
    simplefs_inode_t* new_inode = &fs_data->inode_table[new_inode_num];
    new_inode->mode = SIMPLEFS_S_IFREG | SIMPLEFS_S_IRUSR | SIMPLEFS_S_IWUSR | SIMPLEFS_S_IRGRP | SIMPLEFS_S_IROTH; // 0644
    
    // Set ownership from current process (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        new_inode->uid = (uint16_t)proc->owner_id;
        new_inode->owner_type = (uint8_t)proc->owner_type;
    } else {
        new_inode->uid = 0;
        new_inode->owner_type = (uint8_t)OWNER_SYSTEM;
    }
    
    new_inode->gid = 0;  // Root group
    new_inode->size = 0;
    new_inode->size_high = 0;
    new_inode->atime = get_current_time();
    new_inode->ctime = get_current_time();
    new_inode->mtime = get_current_time();
    new_inode->dtime = 0;
    new_inode->links_count = 1;
    new_inode->blocks = 0;
    new_inode->flags = 0;
    new_inode->indirect_ptr = 0;
    new_inode->double_indirect_ptr = 0;
    new_inode->triple_indirect_ptr = 0;
    new_inode->generation = 0;
    new_inode->file_acl = 0;
    write_inode(fs_data, new_inode_num);
    
    // Add directory entry to parent
    uint32_t parent_inode_num = (uint32_t)parent->fs_data;
    simplefs_inode_t* parent_inode = &fs_data->inode_table[parent_inode_num];
    
    serial_puts("SimpleFS: Adding directory entry to parent inode ");
    itoa(parent_inode_num, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    // Find space for new directory entry
    uint8_t block_buffer[SIMPLEFS_BLOCK_SIZE];
    
    for (uint32_t block_idx = 0; block_idx < SIMPLEFS_DIRECT_BLOCKS; block_idx++) {
        if (parent_inode->block_ptrs[block_idx] == 0) {
            // Allocate new block for directory
            serial_puts("SimpleFS: Allocating new directory block\n");
            uint32_t new_block = alloc_block(fs_data);
            if (new_block == 0) {
                serial_puts("SimpleFS: Failed to allocate directory block\n");
                free_inode(fs_data, new_inode_num);
                return NULL;
            }
            parent_inode->block_ptrs[block_idx] = new_block;
            parent_inode->blocks++;
            memset(block_buffer, 0, SIMPLEFS_BLOCK_SIZE);
            
            serial_puts("SimpleFS: Allocated directory block ");
            itoa(new_block, buf, 10);
            serial_puts(buf);
            serial_puts("\n");
        } else {
            read_block(fs_data, parent_inode->block_ptrs[block_idx], block_buffer);
        }
        
        // Find free space in block
        uint32_t offset = 0;
        while (offset < SIMPLEFS_BLOCK_SIZE - sizeof(simplefs_dirent_t)) {
            simplefs_dirent_t* dirent = (simplefs_dirent_t*)(block_buffer + offset);
            
            if (dirent->inode == 0) {
                // Found free space
                serial_puts("SimpleFS: Found free space at offset ");
                itoa(offset, buf, 10);
                serial_puts(buf);
                serial_puts("\n");
                
                dirent->inode = new_inode_num;
                dirent->rec_len = sizeof(simplefs_dirent_t);
                dirent->name_len = strlen(name);
                dirent->file_type = VFS_FILE; // Regular file
                strncpy(dirent->name, name, SIMPLEFS_MAX_FILENAME);
                dirent->name[SIMPLEFS_MAX_FILENAME] = '\0';
                
                serial_puts("SimpleFS: Writing directory block\n");
                write_block(fs_data, parent_inode->block_ptrs[block_idx], block_buffer);
                write_inode(fs_data, parent_inode_num);
                
                serial_puts("SimpleFS: File created successfully\n");
                return simplefs_inode_to_vnode(fs_data, new_inode_num, name, parent->fs);
            }
            
            if (dirent->rec_len == 0) {
                offset = SIMPLEFS_BLOCK_SIZE; // End of entries
                break;
            }
            
            offset += dirent->rec_len;
        }
    }
    
    // No space for new entry
    serial_puts("SimpleFS: No space for new directory entry\n");
    free_inode(fs_data, new_inode_num);
    return NULL;
}

static int simplefs_vnode_unlink(vnode_t* parent, const char* name) {
    if (!parent || !name || parent->type != VFS_DIRECTORY) {
        serial_puts("SimpleFS: unlink failed - invalid parent or name\n");
        return VFS_ERR_INVALID;
    }
    
    serial_puts("SimpleFS: Unlinking '");
    serial_puts(name);
    serial_puts("'\n");
    
    simplefs_data_t* fs_data = (simplefs_data_t*)parent->fs->fs_data;
    uint32_t parent_inode_num = (uint32_t)parent->fs_data;
    simplefs_inode_t* parent_inode = &fs_data->inode_table[parent_inode_num];
    
    uint8_t block_buffer[SIMPLEFS_BLOCK_SIZE];
    
    // Search for the file in parent directory
    for (uint32_t block_idx = 0; block_idx < SIMPLEFS_DIRECT_BLOCKS && parent_inode->block_ptrs[block_idx] != 0; block_idx++) {
        if (read_block(fs_data, parent_inode->block_ptrs[block_idx], block_buffer) != 0) {
            continue;
        }
        
        uint32_t offset = 0;
        while (offset < SIMPLEFS_BLOCK_SIZE) {
            simplefs_dirent_t* dirent = (simplefs_dirent_t*)(block_buffer + offset);
            
            if (dirent->inode == 0 || dirent->rec_len == 0) {
                break;
            }
            
            if (dirent->name_len > 0 && strcmp(dirent->name, name) == 0) {
                uint32_t inode_num = dirent->inode;
                simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
                
                // Check if it's a directory and not empty
                if (SIMPLEFS_ISDIR(inode->mode)) {
                    // Check if directory is empty
                    for (uint32_t i = 0; i < SIMPLEFS_DIRECT_BLOCKS && inode->block_ptrs[i] != 0; i++) {
                        uint8_t dir_block[SIMPLEFS_BLOCK_SIZE];
                        if (read_block(fs_data, inode->block_ptrs[i], dir_block) == 0) {
                            simplefs_dirent_t* entry = (simplefs_dirent_t*)dir_block;
                            if (entry->inode != 0) {
                                serial_puts("SimpleFS: Directory not empty\n");
                                return VFS_ERR_NOTEMPTY;
                            }
                        }
                    }
                }
                
                serial_puts("SimpleFS: Freeing inode blocks\n");
                // Free all blocks used by the file/directory
                free_inode_blocks(fs_data, inode_num);
                
                serial_puts("SimpleFS: Freeing inode\n");
                // Free the inode
                free_inode(fs_data, inode_num);
                
                // Mark directory entry as free
                dirent->inode = 0;
                dirent->name_len = 0;
                
                // Write back directory block
                write_block(fs_data, parent_inode->block_ptrs[block_idx], block_buffer);
                
                serial_puts("SimpleFS: File deleted successfully\n");
                return VFS_OK;
            }
            
            offset += dirent->rec_len;
        }
    }
    
    serial_puts("SimpleFS: File not found\n");
    return VFS_ERR_NOTFOUND;
}

static int simplefs_vnode_mkdir(vnode_t* parent, const char* name) {
    if (!parent || !name || parent->type != VFS_DIRECTORY) {
        return VFS_ERR_INVALID;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)parent->fs->fs_data;
    
    // Allocate new inode for directory
    uint32_t new_inode_num = alloc_inode(fs_data);
    if (new_inode_num == 0) {
        return VFS_ERR_NOSPACE;
    }
    
    // Initialize directory inode
    simplefs_inode_t* new_inode = &fs_data->inode_table[new_inode_num];
    new_inode->mode = SIMPLEFS_S_IFDIR | SIMPLEFS_S_IRWXU | SIMPLEFS_S_IRGRP | SIMPLEFS_S_IXGRP | SIMPLEFS_S_IROTH | SIMPLEFS_S_IXOTH; // 0755
    
    // Set ownership from current process (v0.7.3)
    process_t* proc = process_get_current();
    if (proc) {
        new_inode->uid = (uint16_t)proc->owner_id;
        new_inode->owner_type = (uint8_t)proc->owner_type;
    } else {
        new_inode->uid = 0;
        new_inode->owner_type = (uint8_t)OWNER_SYSTEM;
    }
    
    new_inode->gid = 0;
    new_inode->size = 0;
    new_inode->size_high = 0;
    new_inode->atime = get_current_time();
    new_inode->ctime = get_current_time();
    new_inode->mtime = get_current_time();
    new_inode->dtime = 0;
    new_inode->links_count = 2; // . and parent's reference
    new_inode->blocks = 0;
    new_inode->flags = 0;
    new_inode->indirect_ptr = 0;
    new_inode->double_indirect_ptr = 0;
    new_inode->triple_indirect_ptr = 0;
    new_inode->generation = 0;
    new_inode->file_acl = 0;
    write_inode(fs_data, new_inode_num);
    
    // Add directory entry to parent (similar to create)
    uint32_t parent_inode_num = (uint32_t)parent->fs_data;
    simplefs_inode_t* parent_inode = &fs_data->inode_table[parent_inode_num];
    
    uint8_t block_buffer[SIMPLEFS_BLOCK_SIZE];
    
    for (uint32_t block_idx = 0; block_idx < SIMPLEFS_DIRECT_BLOCKS; block_idx++) {
        if (parent_inode->block_ptrs[block_idx] == 0) {
            uint32_t new_block = alloc_block(fs_data);
            if (new_block == 0) {
                free_inode(fs_data, new_inode_num);
                return VFS_ERR_NOSPACE;
            }
            parent_inode->block_ptrs[block_idx] = new_block;
            parent_inode->blocks++;
            memset(block_buffer, 0, SIMPLEFS_BLOCK_SIZE);
        } else {
            read_block(fs_data, parent_inode->block_ptrs[block_idx], block_buffer);
        }
        
        uint32_t offset = 0;
        while (offset < SIMPLEFS_BLOCK_SIZE - sizeof(simplefs_dirent_t)) {
            simplefs_dirent_t* dirent = (simplefs_dirent_t*)(block_buffer + offset);
            
            if (dirent->inode == 0) {
                dirent->inode = new_inode_num;
                dirent->rec_len = sizeof(simplefs_dirent_t);
                dirent->name_len = strlen(name);
                dirent->file_type = VFS_DIRECTORY; // Directory
                strncpy(dirent->name, name, SIMPLEFS_MAX_FILENAME);
                dirent->name[SIMPLEFS_MAX_FILENAME] = '\0';
                
                write_block(fs_data, parent_inode->block_ptrs[block_idx], block_buffer);
                write_inode(fs_data, parent_inode_num);
                
                return VFS_OK;
            }
            
            if (dirent->rec_len == 0) {
                offset = SIMPLEFS_BLOCK_SIZE;
                break;
            }
            
            offset += dirent->rec_len;
        }
    }
    
    free_inode(fs_data, new_inode_num);
    return VFS_ERR_NOSPACE;
}

static int simplefs_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent) {
    if (!node || !dirent || node->type != VFS_DIRECTORY) {
        return VFS_ERR_INVALID;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)node->fs->fs_data;
    uint32_t inode_num = (uint32_t)node->fs_data;
    simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
    
    uint8_t block_buffer[SIMPLEFS_BLOCK_SIZE];
    uint32_t current_index = 0;
    
    for (uint32_t block_idx = 0; block_idx < SIMPLEFS_DIRECT_BLOCKS && inode->block_ptrs[block_idx] != 0; block_idx++) {
        if (read_block(fs_data, inode->block_ptrs[block_idx], block_buffer) != 0) {
            continue;
        }
        
        uint32_t offset = 0;
        while (offset < SIMPLEFS_BLOCK_SIZE) {
            simplefs_dirent_t* dir_ent = (simplefs_dirent_t*)(block_buffer + offset);
            
            if (dir_ent->inode == 0 || dir_ent->rec_len == 0) {
                break;
            }
            
            if (current_index == index) {
                strncpy(dirent->name, dir_ent->name, 255);
                dirent->name[255] = '\0';
                dirent->inode = dir_ent->inode;
                dirent->type = dir_ent->file_type;
                return VFS_OK;
            }
            
            current_index++;
            offset += dir_ent->rec_len;
        }
    }
    
    return VFS_ERR_NOTFOUND;
}

int simplefs_get_stats(simplefs_superblock_t* stats) {
    if (!stats) {
        return -1;
    }
    
    // Find the mounted simplefs instance
    for (int i = 0; i < 32; i++) {
        mount_t* mount = vfs_get_mount(i);
        if (mount && mount->fs && strcmp(mount->fs->name, "simplefs") == 0) {
            simplefs_data_t* fs_data = (simplefs_data_t*)mount->fs->fs_data;
            if (fs_data) {
                memcpy(stats, &fs_data->superblock, sizeof(simplefs_superblock_t));
                return 0;
            }
        }
    }
    
    return -1;
}

static int simplefs_vnode_stat(vnode_t* node, stat_t* stat) {
    if (!node || !stat) {
        return VFS_ERR_INVALID;
    }
    
    simplefs_data_t* fs_data = (simplefs_data_t*)node->fs->fs_data;
    uint32_t inode_num = (uint32_t)node->fs_data;
    simplefs_inode_t* inode = &fs_data->inode_table[inode_num];
    
    stat->st_ino = inode_num;
    stat->st_mode = inode->mode;
    stat->st_size = inode->size;
    stat->st_blocks = inode->blocks;
    stat->st_blksize = SIMPLEFS_BLOCK_SIZE;
    
    return VFS_OK;
}

// Public API

void simplefs_init(void) {
    serial_puts("Initializing SimpleFS driver...\n");
    
    // Register filesystem with VFS
    vfs_register_filesystem(&simplefs_filesystem);
    
    serial_puts("SimpleFS driver initialized.\n");
}

filesystem_t* simplefs_get_fs(void) {
    return &simplefs_filesystem;
}

int simplefs_format(uint32_t start_lba, uint32_t num_blocks) {
    serial_puts("Formatting disk with SimpleFS v2...\n");
    
    if (!ata_drive_available()) {
        serial_puts("SimpleFS: No ATA drive available for formatting\n");
        return -1;
    }
    
    if (num_blocks > SIMPLEFS_MAX_BLOCKS) {
        num_blocks = SIMPLEFS_MAX_BLOCKS;
    }
    
    // Create superblock
    simplefs_superblock_t superblock;
    memset(&superblock, 0, sizeof(superblock));
    
    superblock.magic = SIMPLEFS_MAGIC;
    superblock.version = SIMPLEFS_VERSION;
    superblock.block_size = SIMPLEFS_BLOCK_SIZE;
    superblock.total_blocks = num_blocks;
    superblock.total_inodes = SIMPLEFS_MAX_INODES;
    superblock.block_bitmap_block = 1;
    superblock.inode_bitmap_block = 2;
    superblock.inode_table_block = 3;
    
    // Calculate inode table size
    uint32_t inodes_per_block = SIMPLEFS_BLOCK_SIZE / sizeof(simplefs_inode_t);
    uint32_t inode_blocks = (SIMPLEFS_MAX_INODES + inodes_per_block - 1) / inodes_per_block;
    
    // Reserve space for journal (64 blocks = 32KB)
    superblock.journal_size = 64;
    superblock.journal_block = 3 + inode_blocks;
    
    superblock.first_data_block = superblock.journal_block + superblock.journal_size;
    superblock.free_blocks = num_blocks - superblock.first_data_block;
    superblock.free_inodes = SIMPLEFS_MAX_INODES - 1; // -1 for root directory
    superblock.mount_count = 0;
    superblock.max_mount_count = 20;
    superblock.state = SIMPLEFS_JOURNAL_CLEAN;
    superblock.last_mount_time = 0;
    superblock.last_write_time = get_current_time();
    superblock.last_check_time = get_current_time();
    strncpy(superblock.volume_name, "aOS-SimpleFS", 31);
    superblock.volume_name[31] = '\0';
    
    // Write superblock
    if (ata_write_sectors(start_lba, 1, (const uint8_t*)&superblock) != 0) {
        serial_puts("SimpleFS: Failed to write superblock\n");
        return -1;
    }
    
    // Initialize block bitmap
    uint8_t bitmap[SIMPLEFS_BLOCK_SIZE];
    memset(bitmap, 0, SIMPLEFS_BLOCK_SIZE);
    
    // Mark metadata blocks as allocated
    for (uint32_t i = 0; i < superblock.first_data_block; i++) {
        bitmap[i / 8] |= (1 << (i % 8));
    }
    
    ata_write_sectors(start_lba + 1, 1, bitmap);
    
    // Initialize inode bitmap
    memset(bitmap, 0, SIMPLEFS_BLOCK_SIZE);
    bitmap[0] |= 0x03; // Mark inodes 0 and 1 as allocated (0=reserved, 1=root)
    ata_write_sectors(start_lba + 2, 1, bitmap);
    
    // Initialize inode table
    uint8_t inode_block[SIMPLEFS_BLOCK_SIZE];
    memset(inode_block, 0, SIMPLEFS_BLOCK_SIZE);
    
    // Create root directory inode (inode 1)
    simplefs_inode_t* root_inode = (simplefs_inode_t*)inode_block;
    root_inode[1].mode = SIMPLEFS_S_IFDIR | SIMPLEFS_S_IRWXU | SIMPLEFS_S_IRGRP | SIMPLEFS_S_IXGRP | SIMPLEFS_S_IROTH | SIMPLEFS_S_IXOTH;
    root_inode[1].uid = 0;
    root_inode[1].gid = 0;
    root_inode[1].size = 0;
    root_inode[1].size_high = 0;
    root_inode[1].atime = get_current_time();
    root_inode[1].ctime = get_current_time();
    root_inode[1].mtime = get_current_time();
    root_inode[1].dtime = 0;
    root_inode[1].links_count = 2;
    root_inode[1].blocks = 0;
    root_inode[1].flags = 0;
    root_inode[1].indirect_ptr = 0;
    root_inode[1].double_indirect_ptr = 0;
    root_inode[1].triple_indirect_ptr = 0;
    
    // Debug: Show root inode data before writing
    serial_puts("SimpleFS format: Root inode mode=");
    char dbuf[16];
    itoa(root_inode[1].mode, dbuf, 16);
    serial_puts(dbuf);
    serial_puts(", writing to LBA ");
    itoa(start_lba + 3, dbuf, 10);
    serial_puts(dbuf);
    serial_puts("\n");
    
    // Write ONLY the first inode block (contains root directory)
    // Other inode blocks will be zero-initialized on demand when inodes are allocated
    // This dramatically speeds up formatting and reduces disk wear
    int write_result = ata_write_sectors(start_lba + 3, 1, inode_block);
    if (write_result != 0) {
        serial_puts("SimpleFS format: FAILED to write root inode block!\n");
        return -1;
    }
    
    // Verify the write succeeded
    uint8_t verify_block[SIMPLEFS_BLOCK_SIZE];
    if (ata_read_sectors(start_lba + 3, 1, verify_block) == 0) {
        simplefs_inode_t* verify_inode = (simplefs_inode_t*)verify_block;
        serial_puts("SimpleFS format: Verified inode[1].mode=");
        itoa(verify_inode[1].mode, dbuf, 16);
        serial_puts(dbuf);
        serial_puts("\n");
        
        if (verify_inode[1].mode != root_inode[1].mode) {
            serial_puts("SimpleFS format: Verification MISMATCH!\n");
            return -1;
        }
    } else {
        serial_puts("SimpleFS format: Verification read failed\n");
        return -1;
    }
    
    // Note: We skip writing remaining inode blocks - they're implicitly zero
    // The inode bitmap tracks allocation, so unallocated inodes don't need to exist on disk
    serial_puts("SimpleFS format: Skipping pre-initialization of ");
    itoa(inode_blocks - 1, dbuf, 10);
    serial_puts(dbuf);
    serial_puts(" empty inode blocks (lazy allocation)\n");

    // Initialize journal (optimized: batch write)
    // Write all journal blocks at once if possible
    uint32_t journal_bytes = superblock.journal_size * SIMPLEFS_BLOCK_SIZE;
    if (journal_bytes <= 4096) {
        uint8_t journal_buf[4096];
        memset(journal_buf, 0, journal_bytes);
        ata_write_sectors(start_lba + superblock.journal_block, superblock.journal_size, journal_buf);
    } else {
        uint8_t* journal_buf = (uint8_t*)kmalloc(journal_bytes);
        if (journal_buf) {
            memset(journal_buf, 0, journal_bytes);
            ata_write_sectors(start_lba + superblock.journal_block, superblock.journal_size, journal_buf);
            kfree(journal_buf);
        } else {
            // Fallback: write one by one if allocation fails
            // Use a separate zero block for journal
            uint8_t zero_block[SIMPLEFS_BLOCK_SIZE];
            memset(zero_block, 0, SIMPLEFS_BLOCK_SIZE);
            for (uint32_t i = 0; i < superblock.journal_size; i++) {
                ata_write_sectors(start_lba + superblock.journal_block + i, 1, zero_block);
            }
        }
    }
    
    serial_puts("SimpleFS v2 format complete.\n");
    serial_puts("  Total blocks: ");
    char buf[16];
    itoa(num_blocks, buf, 10);
    serial_puts(buf);
    serial_puts("\n  Total inodes: ");
    itoa(SIMPLEFS_MAX_INODES, buf, 10);
    serial_puts(buf);
    serial_puts("\n  Journal size: ");
    itoa(superblock.journal_size, buf, 10);
    serial_puts(buf);
    serial_puts(" blocks\n");
    
    return 0;
}

// Extended filesystem operations

int simplefs_symlink(const char* target, const char* linkpath) {
    // TODO: Implement symbolic link creation
    // This would create a new inode with mode SIMPLEFS_S_IFLNK
    // and store the target path in the file data
    (void)target;
    (void)linkpath;
    serial_puts("SimpleFS: symlink not yet implemented\n");
    return -1;
}

int simplefs_readlink(const char* path, char* buf, uint32_t bufsize) {
    // TODO: Implement symbolic link reading
    // This would read the target path from a symlink inode
    (void)path;
    (void)buf;
    (void)bufsize;
    serial_puts("SimpleFS: readlink not yet implemented\n");
    return -1;
}

int simplefs_chmod(const char* path, uint16_t mode) {
    // TODO: Implement permission changes
    // This would update the mode field of an inode
    (void)path;
    (void)mode;
    serial_puts("SimpleFS: chmod not yet implemented\n");
    return -1;
}

int simplefs_chown(const char* path, uint16_t uid, uint16_t gid) {
    // TODO: Implement ownership changes
    // This would update the uid/gid fields of an inode
    (void)path;
    (void)uid;
    (void)gid;
    serial_puts("SimpleFS: chown not yet implemented\n");
    return -1;
}

int simplefs_setxattr(const char* path, const char* name, const void* value, uint32_t size) {
    // TODO: Implement extended attributes
    // This would store attributes in the file_acl block
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    serial_puts("SimpleFS: setxattr not yet implemented\n");
    return -1;
}

int simplefs_getxattr(const char* path, const char* name, void* value, uint32_t size) {
    // TODO: Implement extended attribute retrieval
    (void)path;
    (void)name;
    (void)value;
    (void)size;
    serial_puts("SimpleFS: getxattr not yet implemented\n");
    return -1;
}

// Journaling operations

int simplefs_journal_start(simplefs_data_t* fs_data) {
    if (!fs_data || !fs_data->journal_enabled) {
        return -1;
    }
    
    // Mark filesystem as dirty
    fs_data->superblock.state = SIMPLEFS_JOURNAL_DIRTY;
    write_block(fs_data, 0, &fs_data->superblock);
    
    return 0;
}

int simplefs_journal_commit(simplefs_data_t* fs_data) {
    if (!fs_data || !fs_data->journal_enabled) {
        return -1;
    }
    
    // Mark filesystem as clean
    fs_data->superblock.state = SIMPLEFS_JOURNAL_CLEAN;
    fs_data->superblock.last_write_time = get_current_time();
    write_block(fs_data, 0, &fs_data->superblock);
    
    return 0;
}

int simplefs_journal_recover(simplefs_data_t* fs_data) {
    if (!fs_data || !fs_data->journal_enabled) {
        return -1;
    }
    
    serial_puts("SimpleFS: Journal recovery started\n");
    
    // Read journal entries and replay them
    simplefs_journal_entry_t entry;
    for (uint32_t i = 0; i < fs_data->superblock.journal_size; i++) {
        if (read_block(fs_data, fs_data->superblock.journal_block + i, &entry) != 0) {
            continue;
        }
        
        // Check if entry is valid
        if (entry.seq_num == 0 || entry.block_num == 0) {
            continue;
        }
        
        // Verify checksum
        uint32_t checksum = calculate_checksum(entry.data, SIMPLEFS_BLOCK_SIZE - 12);
        if (checksum != entry.checksum) {
            serial_puts("SimpleFS: Journal entry checksum mismatch, skipping\n");
            continue;
        }
        
        // Replay the write
        if (write_block(fs_data, entry.block_num, entry.data) != 0) {
            serial_puts("SimpleFS: Failed to replay journal entry\n");
            return -1;
        }
    }
    
    // Clear journal
    uint8_t zero_block[SIMPLEFS_BLOCK_SIZE];
    memset(zero_block, 0, SIMPLEFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < fs_data->superblock.journal_size; i++) {
        write_block(fs_data, fs_data->superblock.journal_block + i, zero_block);
    }
    
    serial_puts("SimpleFS: Journal recovery complete\n");
    return 0;
}
