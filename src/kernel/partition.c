/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/partition.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <partition.h>
#include <dev/ata.h>
#include <string.h>
#include <serial.h>
#include <fs/vfs.h>

static partition_table_t global_partition_table;

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
    part->filesystem_type = 0;
    part->mounted = 0;
    part->mount_point[0] = '\0';
    
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

int partition_list(void) {
    return global_partition_table.count;
}

partition_t* partition_get(int partition_id) {
    if (partition_id < 0 || partition_id >= global_partition_table.count) {
        return NULL;
    }
    return &global_partition_table.partitions[partition_id];
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
        partition_create("system", PART_TYPE_SYSTEM, 0, total_sectors);
        serial_puts("Created default system partition\n");
    }
    
    return 0;
}

int partition_save_table(void) {
    // Save partition table to sector 1 (sector 0 is boot sector)
    if (!ata_drive_available()) {
        return -1;
    }
    
    char buffer[512];
    memset(buffer, 0, 512);
    
    // Magic signature
    buffer[0] = 'A';
    buffer[1] = 'P';
    buffer[2] = 'T';  // aOS Partition Table
    buffer[3] = global_partition_table.count;
    
    // Write partition entries
    memcpy(buffer + 4, &global_partition_table.partitions,
           sizeof(partition_t) * global_partition_table.count);
    
    return ata_write_sectors(1, 1, (uint8_t*)buffer);
}

int partition_load_table(void) {
    if (!ata_drive_available()) {
        return -1;
    }
    
    char buffer[512];
    if (ata_read_sectors(1, 1, (uint8_t*)buffer) != 0) {
        return -1;
    }
    
    // Check magic signature
    if (buffer[0] != 'A' || buffer[1] != 'P' || buffer[2] != 'T') {
        return -1;  // Invalid signature
    }
    
    global_partition_table.count = buffer[3];
    if (global_partition_table.count > MAX_PARTITIONS) {
        global_partition_table.count = 0;
        return -1;
    }
    
    // Load partition entries
    memcpy(&global_partition_table.partitions, buffer + 4,
           sizeof(partition_t) * global_partition_table.count);
    
    serial_puts("Loaded partition table from disk\n");
    return 0;
}
