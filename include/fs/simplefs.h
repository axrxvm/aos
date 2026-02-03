/*
 * === AOS HEADER BEGIN ===
 * ./include/fs/simplefs.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef SIMPLEFS_H
#define SIMPLEFS_H

#include <fs/vfs.h>
#include <stdint.h>

// SimpleFS: A modern, complete block-based filesystem for aOS
// Inspired by ext2 with modern enhancements
// Block size: 512 bytes (matches ATA sector size)
// Max file size: ~258MB (12 direct + 1-level indirect + 2-level indirect)
// Max files: Limited by inode count
// Note: This filesystem may seem absurd, but it is what i deemed best.

#define SIMPLEFS_MAGIC 0x53465332  // "SFS2" - SimpleFS v2 Signature
#define SIMPLEFS_VERSION 2
#define SIMPLEFS_BLOCK_SIZE 512
#define SIMPLEFS_MAX_FILENAME 255
#define SIMPLEFS_DIRECT_BLOCKS 12  // Increased from 8
#define SIMPLEFS_MAX_INODES 8192   // Increased from 4096
#define SIMPLEFS_MAX_BLOCKS 524288 // 256MB filesystem (increased from 64MB)

// Inode mode flags (Unix-style permissions)
#define SIMPLEFS_S_IFMT   0xF000  // Format mask
#define SIMPLEFS_S_IFSOCK 0xC000  // Socket
#define SIMPLEFS_S_IFLNK  0xA000  // Symbolic link
#define SIMPLEFS_S_IFREG  0x8000  // Regular file
#define SIMPLEFS_S_IFBLK  0x6000  // Block device
#define SIMPLEFS_S_IFDIR  0x4000  // Directory
#define SIMPLEFS_S_IFCHR  0x2000  // Character device
#define SIMPLEFS_S_IFIFO  0x1000  // FIFO

// Permission bits
#define SIMPLEFS_S_ISUID  0x0800  // Set UID
#define SIMPLEFS_S_ISGID  0x0400  // Set GID
#define SIMPLEFS_S_ISVTX  0x0200  // Sticky bit
#define SIMPLEFS_S_IRWXU  0x01C0  // User RWX
#define SIMPLEFS_S_IRUSR  0x0100  // User read
#define SIMPLEFS_S_IWUSR  0x0080  // User write
#define SIMPLEFS_S_IXUSR  0x0040  // User execute
#define SIMPLEFS_S_IRWXG  0x0038  // Group RWX
#define SIMPLEFS_S_IRGRP  0x0020  // Group read
#define SIMPLEFS_S_IWGRP  0x0010  // Group write
#define SIMPLEFS_S_IXGRP  0x0008  // Group execute
#define SIMPLEFS_S_IRWXO  0x0007  // Other RWX
#define SIMPLEFS_S_IROTH  0x0004  // Other read
#define SIMPLEFS_S_IWOTH  0x0002  // Other write
#define SIMPLEFS_S_IXOTH  0x0001  // Other execute

// Journal states
#define SIMPLEFS_JOURNAL_CLEAN 0
#define SIMPLEFS_JOURNAL_DIRTY 1
#define SIMPLEFS_JOURNAL_RECOVERING 2

// Filesystem superblock (first block)
typedef struct simplefs_superblock {
    uint32_t magic;              // Magic number (SIMPLEFS_MAGIC)
    uint32_t version;            // Filesystem version
    uint32_t block_size;         // Block size in bytes
    uint32_t total_blocks;       // Total number of blocks
    uint32_t total_inodes;       // Total number of inodes
    uint32_t free_blocks;        // Free blocks count
    uint32_t free_inodes;        // Free inodes count
    uint32_t first_data_block;   // First data block number
    uint32_t inode_table_block;  // Block number of inode table
    uint32_t block_bitmap_block; // Block number of block bitmap
    uint32_t inode_bitmap_block; // Block number of inode bitmap
    uint32_t journal_block;      // Block number of journal
    uint32_t journal_size;       // Size of journal in blocks
    uint32_t mount_count;        // Number of times mounted
    uint32_t max_mount_count;    // Max mounts before fsck
    uint32_t state;              // Filesystem state (clean/dirty)
    uint32_t last_mount_time;    // Last mount time
    uint32_t last_write_time;    // Last write time
    uint32_t last_check_time;    // Last fsck time
    char volume_name[32];        // Volume name
    uint8_t reserved[396];       // Pad to 512 bytes
} __attribute__((packed)) simplefs_superblock_t;

// Inode structure (128 bytes to fit 4 per block)
typedef struct simplefs_inode {
    uint16_t mode;               // File type and permissions
    uint16_t uid;                // Owner user ID
    uint32_t size;               // File size in bytes (lower 32 bits)
    uint32_t atime;              // Access time
    uint32_t ctime;              // Creation time
    uint32_t mtime;              // Modification time
    uint32_t dtime;              // Deletion time
    uint16_t gid;                // Owner group ID
    uint16_t links_count;        // Hard links count
    uint32_t blocks;             // Number of blocks used
    uint32_t flags;              // File flags
    uint32_t block_ptrs[SIMPLEFS_DIRECT_BLOCKS]; // Direct block pointers
    uint32_t indirect_ptr;       // 1-level indirect block pointer
    uint32_t double_indirect_ptr; // 2-level indirect block pointer
    uint32_t triple_indirect_ptr; // 3-level indirect (reserved for future)
    uint32_t generation;         // File version (for NFS)
    uint32_t file_acl;           // Extended attributes block
    uint32_t size_high;          // File size (upper 32 bits) - for >4GB files
    uint32_t fragment_addr;      // Fragment address (unused)
    uint8_t frag;                // Fragment number
    uint8_t fsize;               // Fragment size
    uint8_t owner_type;          // Owner type (OWNER_SYSTEM, OWNER_ROOT, etc.) - v0.7.3
    uint8_t reserved_pad;        // Padding
    uint32_t reserved2[3];       // Changed from [2] to [3] to pad to 128 bytes
} __attribute__((packed)) simplefs_inode_t;

// Directory entry (variable length)
typedef struct simplefs_dirent {
    uint32_t inode;              // Inode number (0 = unused)
    uint16_t rec_len;            // Record length
    uint8_t name_len;            // Name length
    uint8_t file_type;           // File type (VFS_FILE, VFS_DIRECTORY, etc.)
    char name[SIMPLEFS_MAX_FILENAME + 1]; // Null-terminated name
} __attribute__((packed)) simplefs_dirent_t;

// Journal transaction entry
typedef struct simplefs_journal_entry {
    uint32_t seq_num;            // Sequence number
    uint32_t block_num;          // Block being modified
    uint32_t checksum;           // Data checksum
    uint8_t data[SIMPLEFS_BLOCK_SIZE - 12]; // Block data
} __attribute__((packed)) simplefs_journal_entry_t;

// Extended attribute entry
typedef struct simplefs_xattr {
    uint8_t name_len;            // Attribute name length
    uint8_t value_len;           // Attribute value length  
    char name[32];               // Attribute name
    char value[64];              // Attribute value
} __attribute__((packed)) simplefs_xattr_t;

// In-memory filesystem data
typedef struct simplefs_data {
    simplefs_superblock_t superblock;
    uint8_t* block_bitmap;       // Block allocation bitmap
    uint8_t* inode_bitmap;       // Inode allocation bitmap
    simplefs_inode_t* inode_table; // Inode table
    uint32_t start_lba;          // Starting LBA on disk
    uint32_t journal_seq;        // Current journal sequence number
    uint8_t journal_enabled;     // Journal enabled flag
    uint32_t dirty_count;        // Number of dirty blocks
} simplefs_data_t;

// File type macros
#define SIMPLEFS_ISREG(m)  (((m) & SIMPLEFS_S_IFMT) == SIMPLEFS_S_IFREG)
#define SIMPLEFS_ISDIR(m)  (((m) & SIMPLEFS_S_IFMT) == SIMPLEFS_S_IFDIR)
#define SIMPLEFS_ISLNK(m)  (((m) & SIMPLEFS_S_IFMT) == SIMPLEFS_S_IFLNK)
#define SIMPLEFS_ISCHR(m)  (((m) & SIMPLEFS_S_IFMT) == SIMPLEFS_S_IFCHR)
#define SIMPLEFS_ISBLK(m)  (((m) & SIMPLEFS_S_IFMT) == SIMPLEFS_S_IFBLK)
#define SIMPLEFS_ISFIFO(m) (((m) & SIMPLEFS_S_IFMT) == SIMPLEFS_S_IFIFO)
#define SIMPLEFS_ISSOCK(m) (((m) & SIMPLEFS_S_IFMT) == SIMPLEFS_S_IFSOCK)

// Initialize simplefs filesystem driver
void simplefs_init(void);

// Get simplefs filesystem type
filesystem_t* simplefs_get_fs(void);

// Format a disk with simplefs
int simplefs_format(uint32_t start_lba, uint32_t num_blocks);

// Get filesystem statistics
int simplefs_get_stats(simplefs_superblock_t* stats);

// Extended operations
int simplefs_symlink(const char* target, const char* linkpath);
int simplefs_readlink(const char* path, char* buf, uint32_t bufsize);
int simplefs_chmod(const char* path, uint16_t mode);
int simplefs_chown(const char* path, uint16_t uid, uint16_t gid);
int simplefs_setxattr(const char* path, const char* name, const void* value, uint32_t size);
int simplefs_getxattr(const char* path, const char* name, void* value, uint32_t size);

// Journaling operations
int simplefs_journal_start(simplefs_data_t* fs_data);
int simplefs_journal_commit(simplefs_data_t* fs_data);
int simplefs_journal_recover(simplefs_data_t* fs_data);

#endif // SIMPLEFS_H
