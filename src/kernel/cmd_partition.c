/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/cmd_partition.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <partition.h>

extern void kprint(const char *str);

static void cmd_partitions(const char* args) {
    (void)args;
    
    int count = partition_list();
    if (count == 0) {
        kprint("No partitions found");
        return;
    }
    
    kprint("Disk Partitions:");
    kprint("ID  NAME            TYPE    START       SIZE        MOUNT");
    kprint("--  --------------  ------  ----------  ----------  --------");
    
    for (int i = 0; i < count; i++) {
        partition_t* part = partition_get(i);
        if (part) {
            char line[128];
            char id_str[8], start_str[16], size_str[16];
            
            itoa(i, id_str, 10);
            itoa(part->start_sector, start_str, 10);
            itoa(part->sector_count, size_str, 10);
            
            const char* type_str;
            switch (part->type) {
                case PART_TYPE_SYSTEM: type_str = "SYSTEM"; break;
                case PART_TYPE_DATA:   type_str = "DATA  "; break;
                case PART_TYPE_SWAP:   type_str = "SWAP  "; break;
                default:               type_str = "EMPTY "; break;
            }
            
            strcpy(line, id_str);
            strcat(line, "   ");
            strcat(line, part->name);
            strcat(line, "  ");
            strcat(line, type_str);
            strcat(line, "  ");
            strcat(line, start_str);
            strcat(line, "  ");
            strcat(line, size_str);
            strcat(line, "  ");
            if (part->mounted) {
                strcat(line, part->mount_point);
            } else {
                strcat(line, "(unmounted)");
            }
            
            kprint(line);
        }
    }
}

static void cmd_partmount(const char* args) {
    if (!args || strlen(args) == 0) {
        kprint("Usage: partmount <partition_id> <mount_point> <fs_type>");
        return;
    }
    
    int id = 0;
    const char* p = args;
    while (*p >= '0' && *p <= '9') {
        id = id * 10 + (*p - '0');
        p++;
    }
    
    while (*p == ' ') p++;
    const char* mount_point = p;
    while (*p && *p != ' ') p++;
    
    char mount_str[64] = {0};
    strncpy(mount_str, mount_point, p - mount_point);
    
    while (*p == ' ') p++;
    const char* fs_type = p;
    
    if (strlen(mount_str) == 0 || strlen(fs_type) == 0) {
        kprint("Error: Missing arguments");
        return;
    }
    
    if (partition_mount(id, mount_str, fs_type) == 0) {
        kprint("Partition mounted successfully");
    } else {
        kprint("Error: Failed to mount partition");
    }
}

void cmd_module_partition_register(void) {
    command_register_with_category("partitions", "", "List disk partitions", "Partition", cmd_partitions);
    command_register_with_category("partmount", "<id> <path> <fs>", "Mount partition", "Partition", cmd_partmount);
}
