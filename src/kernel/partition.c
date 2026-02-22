/*
 * === AOS HEADER BEGIN ===
 * src/kernel/partition.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <partition.h>
#include <dev/ata.h>
#include <string.h>
#include <serial.h>
#include <fs/vfs.h>

static partition_table_t global_partition_table;

#define APT_DISK_MAGIC_0 'A'
#define APT_DISK_MAGIC_1 'P'
#define APT_DISK_MAGIC_2 'T'
#define APT_DISK_MARKER  0xA1
#define APT_DISK_VERSION 1
#define APT_DISK_HEADER_SIZE 8

typedef struct __attribute__((packed)) {
    char name[PARTITION_NAME_LEN];
    uint8_t type;
    uint8_t active;
    uint16_t reserved;
    uint32_t start_sector;
    uint32_t sector_count;
    uint32_t filesystem_type;
} partition_disk_entry_t;

static void partition_reset_runtime_fields(partition_t* part) {
    if (!part) {
        return;
    }
    part->mounted = 0;
    part->mount_point[0] = '\0';
}

void init_partitions(void) {
    serial_puts("Initializing partition manager...\n");
    memset(&global_partition_table, 0, sizeof(partition_table_t));
    global_partition_table.count = 0;
    
    // Try to load existing partition table
    if (partition_load_table() != 0) {
        serial_puts("No existing partition table found\n");
    }
    
    serial_puts("Partition manager initialized.\n");
}

int partition_create(const char* name, uint8_t type, uint32_t start_sector, uint32_t size_sectors) {
    if (global_partition_table.count >= MAX_PARTITIONS) {
        return -1;  // No space
    }
    
    int id = global_partition_table.count;
    partition_t* part = &global_partition_table.partitions[id];
    
    strncpy(part->name, name, PARTITION_NAME_LEN - 1);
    part->name[PARTITION_NAME_LEN - 1] = '\0';
    part->type = type;
    part->active = 0;
    part->start_sector = start_sector;
    part->sector_count = size_sectors;
    part->filesystem_type = PART_FS_UNKNOWN;
    partition_reset_runtime_fields(part);
    
    global_partition_table.count++;
    
    return id;
}

int partition_delete(int partition_id) {
    if (partition_id < 0 || partition_id >= global_partition_table.count) {
        return -1;
    }
    
    partition_t* part = &global_partition_table.partitions[partition_id];
    
    // Unmount if mounted
    if (part->mounted) {
        partition_unmount(partition_id);
    }
    
    // Shift remaining partitions
    for (int i = partition_id; i < global_partition_table.count - 1; i++) {
        memcpy(&global_partition_table.partitions[i],
               &global_partition_table.partitions[i + 1],
               sizeof(partition_t));
    }
    
    global_partition_table.count--;
    return 0;
}

void partition_clear(void) {
    memset(&global_partition_table, 0, sizeof(global_partition_table));
}

int partition_list(void) {
    return global_partition_table.count;
}

partition_t* partition_get(int partition_id) {
    if (partition_id < 0 || partition_id >= global_partition_table.count) {
        return NULL;
    }
    return &global_partition_table.partitions[partition_id];
}

int partition_find_first_by_type(uint8_t type) {
    for (int i = 0; i < global_partition_table.count; i++) {
        if (global_partition_table.partitions[i].type == type) {
            return i;
        }
    }
    return -1;
}

int partition_find_first_by_type_and_fs(uint8_t type, uint32_t filesystem_type) {
    for (int i = 0; i < global_partition_table.count; i++) {
        partition_t* part = &global_partition_table.partitions[i];
        if (part->type == type && part->filesystem_type == filesystem_type) {
            return i;
        }
    }
    return -1;
}

int partition_mount(int partition_id, const char* mount_point, const char* fs_type) {
    partition_t* part = partition_get(partition_id);
    if (!part || part->mounted) {
        return -1;
    }
    
    // Mount using VFS
    if (vfs_mount(NULL, mount_point, fs_type, 0) != 0) {
        return -1;
    }
    
    strncpy(part->mount_point, mount_point, 31);
    part->mount_point[31] = '\0';
    part->mounted = 1;
    
    return 0;
}

int partition_unmount(int partition_id) {
    partition_t* part = partition_get(partition_id);
    if (!part || !part->mounted) {
        return -1;
    }
    
    // Unmount using VFS
    vfs_unmount(part->mount_point);
    
    part->mounted = 0;
    part->mount_point[0] = '\0';
    
    return 0;
}

int partition_scan_disk(void) {
    // For now, create a single partition using entire disk
    if (!ata_drive_available()) {
        return -1;
    }
    
    uint32_t total_sectors = ata_get_sector_count();
    
    // Create default partition if none exist
    if (global_partition_table.count == 0) {
        int part_id = partition_create("system", PART_TYPE_SYSTEM, 0, total_sectors);
        if (part_id >= 0) {
            partition_t* part = partition_get(part_id);
            if (part) {
                part->filesystem_type = PART_FS_UNKNOWN;
            }
        }
        serial_puts("Created default system partition\n");
    }
    
    return 0;
}

int partition_save_table(void) {
    // Save partition table to sector 1 (sector 0 is boot sector)
    if (!ata_drive_available()) {
        return -1;
    }
    
    uint8_t buffer[512];
    memset(buffer, 0, sizeof(buffer));

    buffer[0] = APT_DISK_MAGIC_0;
    buffer[1] = APT_DISK_MAGIC_1;
    buffer[2] = APT_DISK_MAGIC_2;
    buffer[3] = APT_DISK_MARKER;
    buffer[4] = APT_DISK_VERSION;
    buffer[5] = (uint8_t)global_partition_table.count;

    uint32_t entry_bytes = sizeof(partition_disk_entry_t) * (uint32_t)global_partition_table.count;
    if (APT_DISK_HEADER_SIZE + entry_bytes > sizeof(buffer)) {
        return -1;
    }

    for (int i = 0; i < global_partition_table.count; i++) {
        const partition_t* src = &global_partition_table.partitions[i];
        partition_disk_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.name, src->name, PARTITION_NAME_LEN - 1);
        entry.type = src->type;
        entry.active = src->active;
        entry.start_sector = src->start_sector;
        entry.sector_count = src->sector_count;
        entry.filesystem_type = src->filesystem_type;

        memcpy(buffer + APT_DISK_HEADER_SIZE + (i * sizeof(partition_disk_entry_t)),
               &entry,
               sizeof(entry));
    }

    return ata_write_sectors(1, 1, buffer);
}

int partition_load_table(void) {
    if (!ata_drive_available()) {
        return -1;
    }
    
    uint8_t buffer[512];
    if (ata_read_sectors(1, 1, buffer) != 0) {
        return -1;
    }
    
    // Check magic signature
    if (buffer[0] != APT_DISK_MAGIC_0 || buffer[1] != APT_DISK_MAGIC_1 || buffer[2] != APT_DISK_MAGIC_2) {
        return -1;  // Invalid signature
    }


    memset(&global_partition_table, 0, sizeof(global_partition_table));

    // New on-disk table format
    if (buffer[3] == APT_DISK_MARKER && buffer[4] == APT_DISK_VERSION) {
        global_partition_table.count = (int)buffer[5];
        if (global_partition_table.count < 0 || global_partition_table.count > MAX_PARTITIONS) {
            global_partition_table.count = 0;
            return -1;
        }

        uint32_t entry_bytes = sizeof(partition_disk_entry_t) * (uint32_t)global_partition_table.count;
        if (APT_DISK_HEADER_SIZE + entry_bytes > sizeof(buffer)) {
            global_partition_table.count = 0;
            return -1;
        }

        for (int i = 0; i < global_partition_table.count; i++) {
            partition_disk_entry_t entry;
            memcpy(&entry,
                   buffer + APT_DISK_HEADER_SIZE + (i * sizeof(partition_disk_entry_t)),
                   sizeof(entry));

            partition_t* dst = &global_partition_table.partitions[i];
            memset(dst, 0, sizeof(*dst));
            strncpy(dst->name, entry.name, PARTITION_NAME_LEN - 1);
            dst->name[PARTITION_NAME_LEN - 1] = '\0';
            dst->type = entry.type;
            dst->active = entry.active;
            dst->start_sector = entry.start_sector;
            dst->sector_count = entry.sector_count;
            dst->filesystem_type = entry.filesystem_type;
            partition_reset_runtime_fields(dst);
        }

        serial_puts("Loaded partition table from disk\n");
        return 0;
    }

    // Legacy format compatibility:
    //  [0..2] magic "APT"
    //  [3]    count
    //  [4..]  raw partition_t array
    global_partition_table.count = (int)buffer[3];
    if (global_partition_table.count < 0 || global_partition_table.count > MAX_PARTITIONS) {
        global_partition_table.count = 0;
        return -1;
    }

    uint32_t legacy_bytes = sizeof(partition_t) * (uint32_t)global_partition_table.count;
    if (4 + legacy_bytes > sizeof(buffer)) {
        global_partition_table.count = 0;
        return -1;
    }

    memcpy(&global_partition_table.partitions, buffer + 4, legacy_bytes);
    for (int i = 0; i < global_partition_table.count; i++) {
        partition_reset_runtime_fields(&global_partition_table.partitions[i]);
        if (global_partition_table.partitions[i].filesystem_type == 0) {
            global_partition_table.partitions[i].filesystem_type = PART_FS_UNKNOWN;
        }
    }

    serial_puts("Loaded partition table from disk\n");
    return 0;
}
