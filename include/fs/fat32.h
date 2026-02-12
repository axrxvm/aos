/*
 * === AOS HEADER BEGIN ===
 * include/fs/fat32.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#ifndef FAT32_H
#define FAT32_H

#include <fs/vfs.h>
#include <stdint.h>

// FAT32 filesystem driver for aOS
// Supports reading and writing to FAT32 formatted disks
// Compatible with standard FAT32 implementation

// FAT32 constants
#define FAT32_SIGNATURE_55AA 0xAA55
#define FAT32_EXTENDED_BOOT_SIGNATURE 0x29
#define FAT32_FAT_ID 0x0FFFFFF8
#define FAT32_EOF 0x0FFFFFFF
#define FAT32_BAD_CLUSTER 0x0FFFFFF7
#define FAT32_FREE_CLUSTER 0x00000000

// Cluster chain markers
#define FAT32_CLUSTER_FREE 0x00000000
#define FAT32_CLUSTER_RESERVED 0x0FFFFFF0
#define FAT32_CLUSTER_BAD 0x0FFFFFF7
#define FAT32_CLUSTER_EOC 0x0FFFFFF8  // End of chain (>= this value)

// Directory entry attributes
#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LONG_NAME (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM | FAT32_ATTR_VOLUME_ID)

// Maximum filename length
#define FAT32_MAX_FILENAME 255
#define FAT32_SHORT_NAME_LEN 11

// Sector/cluster sizes
#define FAT32_SECTOR_SIZE 512
#define FAT32_MAX_CLUSTER_SIZE 32768

// FAT32 Boot Sector (BIOS Parameter Block)
typedef struct fat32_boot_sector {
    uint8_t  jump_boot[3];           // Jump instruction
    uint8_t  oem_name[8];            // OEM name
    uint16_t bytes_per_sector;       // Bytes per sector (usually 512)
    uint8_t  sectors_per_cluster;    // Sectors per cluster
    uint16_t reserved_sectors;       // Reserved sectors (usually 32)
    uint8_t  num_fats;               // Number of FAT copies (usually 2)
    uint16_t root_entry_count;       // Root directory entries (0 for FAT32)
    uint16_t total_sectors_16;       // Total sectors (0 for FAT32)
    uint8_t  media_type;             // Media descriptor
    uint16_t fat_size_16;            // FAT size in sectors (0 for FAT32)
    uint16_t sectors_per_track;      // Sectors per track
    uint16_t num_heads;              // Number of heads
    uint32_t hidden_sectors;         // Hidden sectors
    uint32_t total_sectors_32;       // Total sectors (FAT32)
    
    // FAT32 extended fields
    uint32_t fat_size_32;            // FAT size in sectors
    uint16_t ext_flags;              // Extended flags
    uint16_t fs_version;             // Filesystem version
    uint32_t root_cluster;           // Root directory cluster
    uint16_t fs_info;                // FSInfo sector
    uint16_t backup_boot_sector;     // Backup boot sector
    uint8_t  reserved[12];           // Reserved
    uint8_t  drive_number;           // Drive number
    uint8_t  reserved1;              // Reserved
    uint8_t  boot_signature;         // Extended boot signature (0x29)
    uint32_t volume_id;              // Volume ID
    uint8_t  volume_label[11];       // Volume label
    uint8_t  fs_type[8];             // Filesystem type ("FAT32   ")
    uint8_t  boot_code[420];         // Boot code
    uint16_t boot_sector_signature;  // Boot sector signature (0xAA55)
} __attribute__((packed)) fat32_boot_sector_t;

// FAT32 FSInfo structure
typedef struct fat32_fsinfo {
    uint32_t lead_signature;         // 0x41615252
    uint8_t  reserved1[480];         // Reserved
    uint32_t struct_signature;       // 0x61417272
    uint32_t free_clusters;          // Free cluster count (-1 if unknown)
    uint32_t next_free_cluster;      // Next free cluster (hint)
    uint8_t  reserved2[12];          // Reserved
    uint32_t trail_signature;        // 0xAA550000
} __attribute__((packed)) fat32_fsinfo_t;

// FAT32 Directory Entry (8.3 short name)
typedef struct fat32_dir_entry {
    uint8_t  name[11];               // 8.3 filename (padded with spaces)
    uint8_t  attr;                   // File attributes
    uint8_t  nt_reserved;            // Reserved for Windows NT
    uint8_t  creation_time_tenth;    // Creation time (tenths of second)
    uint16_t creation_time;          // Creation time
    uint16_t creation_date;          // Creation date
    uint16_t access_date;            // Last access date
    uint16_t first_cluster_high;     // High word of first cluster
    uint16_t write_time;             // Last write time
    uint16_t write_date;             // Last write date
    uint16_t first_cluster_low;      // Low word of first cluster
    uint32_t file_size;              // File size in bytes
} __attribute__((packed)) fat32_dir_entry_t;

// Long filename entry (VFAT)
typedef struct fat32_lfn_entry {
    uint8_t  order;                  // Order of this entry
    uint16_t name1[5];               // First 5 characters (Unicode)
    uint8_t  attr;                   // Attributes (always 0x0F)
    uint8_t  type;                   // Type (always 0)
    uint8_t  checksum;               // Checksum of short name
    uint16_t name2[6];               // Next 6 characters
    uint16_t first_cluster_low;      // Always 0
    uint16_t name3[2];               // Last 2 characters
} __attribute__((packed)) fat32_lfn_entry_t;

// In-memory filesystem data
typedef struct fat32_data {
    fat32_boot_sector_t boot_sector;
    fat32_fsinfo_t fsinfo;
    uint32_t* fat;                   // File Allocation Table (in-memory cache)
    uint32_t start_lba;              // Starting LBA on disk
    uint32_t fat_start_sector;       // FAT start sector
    uint32_t data_start_sector;      // Data region start sector
    uint32_t bytes_per_cluster;      // Bytes per cluster
    uint32_t total_clusters;         // Total number of clusters
    uint8_t  fat_cache_dirty;        // FAT cache dirty flag
} fat32_data_t;

// In-memory file/directory data
typedef struct fat32_file_data {
    uint32_t first_cluster;          // First cluster of file/directory
    uint32_t current_cluster;        // Current cluster position
    uint32_t cluster_offset;         // Offset within current cluster
    uint32_t file_size;              // File size (0 for directories)
    uint8_t  attributes;             // File attributes
    uint32_t parent_cluster;         // Parent directory cluster (for updating entry)
    char name[256];                  // Filename (for updating entry)
} fat32_file_data_t;

// Initialize FAT32 filesystem driver
void fat32_init(void);

// Get FAT32 filesystem type
filesystem_t* fat32_get_fs(void);

// Format a disk with FAT32 (optional utility function)
int fat32_format(uint32_t start_lba, uint32_t num_sectors, const char* volume_label);

// Get filesystem statistics
int fat32_get_stats(fat32_boot_sector_t* stats);

#endif // FAT32_H
