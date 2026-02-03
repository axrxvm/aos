/*
 * === AOS HEADER BEGIN ===
 * ./include/partition.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>

// aOS Partition Table (simplified, not MBR)
#define MAX_PARTITIONS 8
#define PARTITION_NAME_LEN 16

// Partition types (aOS-style)
#define PART_TYPE_EMPTY    0x00
#define PART_TYPE_SYSTEM   0x01  // Boot/System partition
#define PART_TYPE_DATA     0x02  // User data
#define PART_TYPE_SWAP     0x03  // Swap space
#define PART_TYPE_RAMDISK  0x04  // RAM disk

// Partition structure
typedef struct {
    char name[PARTITION_NAME_LEN];  // Partition label
    uint8_t type;                    // Partition type
    uint8_t active;                  // Bootable flag
    uint32_t start_sector;           // Starting sector
    uint32_t sector_count;           // Number of sectors
    uint32_t filesystem_type;        // Filesystem identifier
    char mount_point[32];            // Where it's mounted
    int mounted;                     // Mount status
} partition_t;

// Partition table manager
typedef struct {
    partition_t partitions[MAX_PARTITIONS];
    int count;
} partition_table_t;

// Initialize partition manager
void init_partitions(void);

// Partition operations
int partition_create(const char* name, uint8_t type, uint32_t start_sector, uint32_t size_sectors);
int partition_delete(int partition_id);
int partition_list(void);
partition_t* partition_get(int partition_id);
int partition_mount(int partition_id, const char* mount_point, const char* fs_type);
int partition_unmount(int partition_id);

// Scan disk for partitions
int partition_scan_disk(void);

// Write partition table to disk
int partition_save_table(void);

// Read partition table from disk
int partition_load_table(void);

#endif // PARTITION_H
