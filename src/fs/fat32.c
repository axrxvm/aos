/*
 * === AOS HEADER BEGIN ===
 * src/fs/fat32.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

// FAT32 filesystem driver for aOS
// 
// Full implementation of FAT32 with VFAT long filename support (up to 255 chars).
// Features: Cluster-based allocation, FAT caching with dirty tracking, immediate sync
// on modifications, directory entry updates on file close for size persistence.
// 
// Supports: Read/write files, create/delete directories, format disk, mount/unmount.
// Compatible with FAT32 partitions created by Windows, Linux, and other OSes.
// Uses 2-level FAT with backup FAT, FSInfo sector for free cluster tracking.
// 
// Cluster size: 4KB (8 sectors), supports disks >32MB, identity-mapped I/O via ATA driver.

#include <fs/fat32.h>
#include <fs/vfs.h>
#include <dev/ata.h>
#include <string.h>
#include <stdlib.h>
#include <serial.h>
#include <vmm.h>
#include <fileperm.h>
#include <process.h>

// Forward declarations for vnode operations
static int fat32_vnode_open(vnode_t* node, uint32_t flags);
static int fat32_vnode_close(vnode_t* node);
static int fat32_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset);
static int fat32_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset);
static vnode_t* fat32_vnode_finddir(vnode_t* node, const char* name);
static vnode_t* fat32_vnode_create(vnode_t* parent, const char* name, uint32_t flags);
static int fat32_vnode_unlink(vnode_t* parent, const char* name);
static int fat32_vnode_mkdir(vnode_t* parent, const char* name);
static int fat32_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent);
static int fat32_vnode_stat(vnode_t* node, stat_t* stat);

// Helper function forward declarations
static int read_sector(fat32_data_t* fs_data, uint32_t sector, void* buffer);
static int write_sector(fat32_data_t* fs_data, uint32_t sector, const void* buffer);
static int read_cluster(fat32_data_t* fs_data, uint32_t cluster, void* buffer);
static int write_cluster(fat32_data_t* fs_data, uint32_t cluster, const void* buffer);
static uint32_t get_next_cluster(fat32_data_t* fs_data, uint32_t cluster);
static int set_next_cluster(fat32_data_t* fs_data, uint32_t cluster, uint32_t next);
static uint32_t alloc_cluster(fat32_data_t* fs_data);
static void free_cluster_chain(fat32_data_t* fs_data, uint32_t first_cluster);
static uint32_t cluster_to_sector(fat32_data_t* fs_data, uint32_t cluster);
static void parse_short_name(const uint8_t* short_name, char* out_name);
static void create_short_name(const char* long_name, uint8_t* short_name);
static uint8_t lfn_checksum(const uint8_t* short_name);
static void parse_lfn_entry(fat32_lfn_entry_t* lfn, char* name_part);
static int find_dir_entry(fat32_data_t* fs_data, uint32_t dir_cluster, const char* name, 
                         fat32_dir_entry_t* entry, uint32_t* entry_sector, uint32_t* entry_offset);
static vnode_t* create_vnode_from_entry(fat32_data_t* fs_data, fat32_dir_entry_t* entry, 
                                       const char* name, filesystem_t* fs, uint32_t parent_cluster);
static int strcasecmp_simple(const char* s1, const char* s2);
static int add_dir_entry(fat32_data_t* fs_data, uint32_t dir_cluster, const char* name, 
                        fat32_dir_entry_t* entry);
static int fat32_sync(fat32_data_t* fs_data);
static int update_dir_entry(fat32_data_t* fs_data, uint32_t dir_cluster, const char* name,
                           fat32_dir_entry_t* new_entry);

// VFS operations table
static vnode_ops_t fat32_vnode_ops = {
    .open = fat32_vnode_open,
    .close = fat32_vnode_close,
    .read = fat32_vnode_read,
    .write = fat32_vnode_write,
    .finddir = fat32_vnode_finddir,
    .create = fat32_vnode_create,
    .unlink = fat32_vnode_unlink,
    .mkdir = fat32_vnode_mkdir,
    .readdir = fat32_vnode_readdir,
    .stat = fat32_vnode_stat
};

// Filesystem operations
static int fat32_mount(filesystem_t* fs, const char* source, uint32_t flags);
static int fat32_unmount(filesystem_t* fs);
static vnode_t* fat32_get_root(filesystem_t* fs);

static filesystem_ops_t fat32_fs_ops = {
    .mount = fat32_mount,
    .unmount = fat32_unmount,
    .get_root = fat32_get_root
};

static filesystem_t fat32_filesystem = {
    .name = "fat32",
    .ops = &fat32_fs_ops,
    .fs_data = NULL,
    .mount = NULL
};


// Helper Functions


static int read_sector(fat32_data_t* fs_data, uint32_t sector, void* buffer) {
    if (!fs_data || !buffer) {
        return -1;
    }
    uint32_t lba = fs_data->start_lba + sector;
    return ata_read_sectors(lba, 1, (uint8_t*)buffer);
}

static int write_sector(fat32_data_t* fs_data, uint32_t sector, const void* buffer) {
    if (!fs_data || !buffer) {
        return -1;
    }
    uint32_t lba = fs_data->start_lba + sector;
    return ata_write_sectors(lba, 1, (const uint8_t*)buffer);
}

static uint32_t cluster_to_sector(fat32_data_t* fs_data, uint32_t cluster) {
    if (cluster < 2) {
        return 0; // Invalid cluster
    }
    return fs_data->data_start_sector + 
           (cluster - 2) * fs_data->boot_sector.sectors_per_cluster;
}

static int read_cluster(fat32_data_t* fs_data, uint32_t cluster, void* buffer) {
    if (!fs_data || !buffer || cluster < 2) {
        return -1;
    }
    
    uint32_t sector = cluster_to_sector(fs_data, cluster);
    uint8_t* buf_ptr = (uint8_t*)buffer;
    
    for (uint32_t i = 0; i < fs_data->boot_sector.sectors_per_cluster; i++) {
        if (read_sector(fs_data, sector + i, buf_ptr + (i * FAT32_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }
    return 0;
}

static int write_cluster(fat32_data_t* fs_data, uint32_t cluster, const void* buffer) {
    if (!fs_data || !buffer || cluster < 2) {
        return -1;
    }
    
    uint32_t sector = cluster_to_sector(fs_data, cluster);
    const uint8_t* buf_ptr = (const uint8_t*)buffer;
    
    for (uint32_t i = 0; i < fs_data->boot_sector.sectors_per_cluster; i++) {
        if (write_sector(fs_data, sector + i, buf_ptr + (i * FAT32_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }
    
    return 0;
}

static uint32_t get_next_cluster(fat32_data_t* fs_data, uint32_t cluster) {
    if (!fs_data || !fs_data->fat || cluster < 2 || cluster >= fs_data->total_clusters + 2) {
        return FAT32_CLUSTER_EOC;
    }
    
    uint32_t next = fs_data->fat[cluster] & 0x0FFFFFFF;
    return next;
}

static int set_next_cluster(fat32_data_t* fs_data, uint32_t cluster, uint32_t next) {
    if (!fs_data || !fs_data->fat || cluster < 2 || cluster >= fs_data->total_clusters + 2) {
        return -1;
    }
    
    fs_data->fat[cluster] = next & 0x0FFFFFFF;
    fs_data->fat_cache_dirty = 1;
    return 0;
}

static uint32_t alloc_cluster(fat32_data_t* fs_data) {
    if (!fs_data || !fs_data->fat) {
        return 0;
    }
    
    // Start from hint in FSInfo if available
    uint32_t start_cluster = (fs_data->fsinfo.next_free_cluster >= 2) ? 
                             fs_data->fsinfo.next_free_cluster : 2;
    
    // Search for free cluster
    for (uint32_t i = start_cluster; i < fs_data->total_clusters + 2; i++) {
        if ((fs_data->fat[i] & 0x0FFFFFFF) == FAT32_CLUSTER_FREE) {
            // Mark as end of chain
            fs_data->fat[i] = FAT32_CLUSTER_EOC;
            fs_data->fat_cache_dirty = 1;
            
            // Update FSInfo
            if (fs_data->fsinfo.free_clusters != 0xFFFFFFFF) {
                fs_data->fsinfo.free_clusters--;
            }
            fs_data->fsinfo.next_free_cluster = i + 1;
            
            // Zero out the cluster
            uint8_t* zero_buf = (uint8_t*)kmalloc(fs_data->bytes_per_cluster);
            if (zero_buf) {
                memset(zero_buf, 0, fs_data->bytes_per_cluster);
                write_cluster(fs_data, i, zero_buf);
                kfree(zero_buf);
            }
            
            return i;
        }
    }
    
    // Wrap around if started from hint
    if (start_cluster > 2) {
        for (uint32_t i = 2; i < start_cluster; i++) {
            if ((fs_data->fat[i] & 0x0FFFFFFF) == FAT32_CLUSTER_FREE) {
                fs_data->fat[i] = FAT32_CLUSTER_EOC;
                fs_data->fat_cache_dirty = 1;
                
                if (fs_data->fsinfo.free_clusters != 0xFFFFFFFF) {
                    fs_data->fsinfo.free_clusters--;
                }
                fs_data->fsinfo.next_free_cluster = i + 1;
                
                uint8_t* zero_buf = (uint8_t*)kmalloc(fs_data->bytes_per_cluster);
                if (zero_buf) {
                    memset(zero_buf, 0, fs_data->bytes_per_cluster);
                    write_cluster(fs_data, i, zero_buf);
                    kfree(zero_buf);
                }
                
                return i;
            }
        }
    }
    
    return 0; // No free clusters
}

static void free_cluster_chain(fat32_data_t* fs_data, uint32_t first_cluster) {
    if (!fs_data || !fs_data->fat || first_cluster < 2) {
        return;
    }
    
    uint32_t cluster = first_cluster;
    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED) {
        uint32_t next = get_next_cluster(fs_data, cluster);
        fs_data->fat[cluster] = FAT32_CLUSTER_FREE;
        
        if (fs_data->fsinfo.free_clusters != 0xFFFFFFFF) {
            fs_data->fsinfo.free_clusters++;
        }
        
        cluster = next;
    }
    
    fs_data->fat_cache_dirty = 1;
}

static void parse_short_name(const uint8_t* short_name, char* out_name) {
    // Parse 8.3 short name into "NAME.EXT" format
    int pos = 0;
    
    // Copy name part (8 chars)
    for (int i = 0; i < 8 && short_name[i] != ' '; i++) {
        out_name[pos++] = (char)short_name[i];
    }
    
    // Add extension if present
    if (short_name[8] != ' ') {
        out_name[pos++] = '.';
        for (int i = 8; i < 11 && short_name[i] != ' '; i++) {
            out_name[pos++] = (char)short_name[i];
        }
    }
    
    out_name[pos] = '\0';
}

static void create_short_name(const char* long_name, uint8_t* short_name) {
    // Create 8.3 short name from long filename
    // Simple algorithm: take first 6 chars of base, add ~1, and first 3 of extension
    
    memset(short_name, ' ', 11);
    
    // Find extension
    const char* ext = NULL;
    for (int i = strlen(long_name) - 1; i >= 0; i--) {
        if (long_name[i] == '.') {
            ext = &long_name[i + 1];
            break;
        }
    }
    
    // Copy base name (up to 6 chars for ~1 suffix)
    int name_len = ext ? (int)(ext - long_name - 1) : (int)strlen(long_name);
    int copy_len = (name_len > 6) ? 6 : name_len;
    
    for (int i = 0; i < copy_len; i++) {
        char c = long_name[i];
        if (c >= 'a' && c <= 'z') {
            c -= 32; // Convert to uppercase
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            short_name[i] = (uint8_t)c;
        } else {
            short_name[i] = '_';
        }
    }
    
    // Add numeric tail if name was truncated
    if (name_len > 6) {
        short_name[6] = '~';
        short_name[7] = '1';
    }
    
    // Copy extension (up to 3 chars)
    if (ext) {
        for (int i = 0; i < 3 && ext[i] != '\0'; i++) {
            char c = ext[i];
            if (c >= 'a' && c <= 'z') {
                c -= 32; // Convert to uppercase
            }
            short_name[8 + i] = (uint8_t)c;
        }
    }
}

static uint8_t lfn_checksum(const uint8_t* short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + short_name[i];
    }
    return sum;
}

static void parse_lfn_entry(fat32_lfn_entry_t* lfn, char* name_part) {
    int pos = 0;
    
    // Parse name1 (5 characters)
    for (int i = 0; i < 5; i++) {
        if (lfn->name1[i] == 0 || lfn->name1[i] == 0xFFFF) break;
        name_part[pos++] = (char)(lfn->name1[i] & 0xFF);
    }
    
    // Parse name2 (6 characters)
    for (int i = 0; i < 6; i++) {
        if (lfn->name2[i] == 0 || lfn->name2[i] == 0xFFFF) break;
        name_part[pos++] = (char)(lfn->name2[i] & 0xFF);
    }
    
    // Parse name3 (2 characters)
    for (int i = 0; i < 2; i++) {
        if (lfn->name3[i] == 0 || lfn->name3[i] == 0xFFFF) break;
        name_part[pos++] = (char)(lfn->name3[i] & 0xFF);
    }
    
    name_part[pos] = '\0';
}

static int strcasecmp_simple(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        
        // Convert to lowercase
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        
        if (c1 != c2) {
            return c1 - c2;
        }
        
        s1++;
        s2++;
    }
    
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    
    return c1 - c2;
}

static int add_dir_entry(fat32_data_t* fs_data, uint32_t dir_cluster, const char* name,
                        fat32_dir_entry_t* entry) {
    if (!fs_data || !name || !entry || dir_cluster < 2) {
        return -1;
    }
    
    uint8_t* cluster_buf = (uint8_t*)kmalloc(fs_data->bytes_per_cluster);
    if (!cluster_buf) {
        return -1;
    }
    
    uint32_t cluster = dir_cluster;
    uint32_t entries_per_cluster = fs_data->bytes_per_cluster / sizeof(fat32_dir_entry_t);
    
    // Search for free entry in existing clusters
    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED) {
        if (read_cluster(fs_data, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            // Found free entry (deleted or end of directory)
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                // Copy the entry
                memcpy(&entries[i], entry, sizeof(fat32_dir_entry_t));
                
                // Write back the cluster
                if (write_cluster(fs_data, cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    return -1;
                }
                
                kfree(cluster_buf);
                return 0;
            }
        }
        
        // Try next cluster in chain
        uint32_t next = get_next_cluster(fs_data, cluster);
        if (next >= FAT32_CLUSTER_RESERVED) {
            // End of chain - need to allocate new cluster
            uint32_t new_cluster = alloc_cluster(fs_data);
            if (new_cluster < 2) {
                kfree(cluster_buf);
                return -1;
            }
            
            // Link to chain
            set_next_cluster(fs_data, cluster, new_cluster);
            
            // Zero new cluster and add entry at start
            memset(cluster_buf, 0, fs_data->bytes_per_cluster);
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
            memcpy(&entries[0], entry, sizeof(fat32_dir_entry_t));
            
            if (write_cluster(fs_data, new_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
            
            kfree(cluster_buf);
            return 0;
        }
        
        cluster = next;
    }
    
    kfree(cluster_buf);
    return -1;
}

// Update an existing directory entry (e.g., to update file size)
static int update_dir_entry(fat32_data_t* fs_data, uint32_t dir_cluster, const char* name,
                           fat32_dir_entry_t* new_entry) {
    if (!fs_data || !name || !new_entry || dir_cluster < 2) {
        return -1;
    }
    
    uint8_t* cluster_buf = (uint8_t*)kmalloc(fs_data->bytes_per_cluster);
    if (!cluster_buf) {
        return -1;
    }
    
    uint32_t cluster = dir_cluster;
    char lfn_name[FAT32_MAX_FILENAME + 1];
    char lfn_parts[20][14];
    int lfn_count = 0;
    
    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED) {
        if (read_cluster(fs_data, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
        uint32_t entries_per_cluster = fs_data->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return -1; // Not found
            }
            
            if (entries[i].name[0] == 0xE5) {
                lfn_count = 0;
                continue;
            }
            
            // Check for LFN
            if ((entries[i].attr & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
                fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)&entries[i];
                int order = lfn->order & 0x1F;
                if (order > 0 && order <= 20) {
                    parse_lfn_entry(lfn, lfn_parts[order - 1]);
                    if (order > lfn_count) {
                        lfn_count = order;
                    }
                }
                continue;
            }
            
            // Regular entry - construct name
            if (lfn_count > 0) {
                lfn_name[0] = '\0';
                for (int j = lfn_count - 1; j >= 0; j--) {
                    strcat(lfn_name, lfn_parts[j]);
                }
            } else {
                parse_short_name(entries[i].name, lfn_name);
            }
            
            // Compare names
            if (strcasecmp_simple(lfn_name, name) == 0) {
                // Update the entry (preserve name and attributes, update size and cluster)
                entries[i].file_size = new_entry->file_size;
                entries[i].first_cluster_low = new_entry->first_cluster_low;
                entries[i].first_cluster_high = new_entry->first_cluster_high;
                
                // Write back the cluster
                if (write_cluster(fs_data, cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    return -1;
                }
                
                kfree(cluster_buf);
                return 0; // Success
            }
            
            lfn_count = 0;
        }
        
        cluster = get_next_cluster(fs_data, cluster);
    }
    
    kfree(cluster_buf);
    return -1; // Not found
}

static int find_dir_entry(fat32_data_t* fs_data, uint32_t dir_cluster, const char* name,
                         fat32_dir_entry_t* entry, uint32_t* entry_sector, uint32_t* entry_offset) {
    if (!fs_data || !name || !entry || dir_cluster < 2) {
        return -1;
    }
    
    uint8_t* cluster_buf = (uint8_t*)kmalloc(fs_data->bytes_per_cluster);
    if (!cluster_buf) {
        return -1;
    }
    
    uint32_t cluster = dir_cluster;
    char lfn_name[FAT32_MAX_FILENAME + 1];
    char lfn_parts[20][14]; // Up to 20 LFN entries
    int lfn_count = 0;
    
    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED) {
        if (read_cluster(fs_data, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
        uint32_t entries_per_cluster = fs_data->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            // Check for end of directory
            if (entries[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return -1; // Not found
            }
            
            // Skip deleted entries
            if (entries[i].name[0] == 0xE5) {
                lfn_count = 0;
                continue;
            }
            
            // Check for LFN entry
            if ((entries[i].attr & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
                fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)&entries[i];
                int order = lfn->order & 0x1F;
                if (order > 0 && order <= 20) {
                    parse_lfn_entry(lfn, lfn_parts[order - 1]);
                    if (order > lfn_count) {
                        lfn_count = order;
                    }
                }
                continue;
            }
            
            // Regular entry - construct full name
            if (lfn_count > 0) {
                // Use long filename
                lfn_name[0] = '\0';
                for (int j = lfn_count - 1; j >= 0; j--) {
                    strcat(lfn_name, lfn_parts[j]);
                }
            } else {
                // Use short filename
                parse_short_name(entries[i].name, lfn_name);
            }
            
            // Compare names (case-insensitive)
            if (strcasecmp_simple(lfn_name, name) == 0) {
                memcpy(entry, &entries[i], sizeof(fat32_dir_entry_t));
                if (entry_sector) {
                    uint32_t sector_in_cluster = (i * sizeof(fat32_dir_entry_t)) / FAT32_SECTOR_SIZE;
                    *entry_sector = cluster_to_sector(fs_data, cluster) + sector_in_cluster;
                }
                if (entry_offset) {
                    *entry_offset = (i * sizeof(fat32_dir_entry_t)) % FAT32_SECTOR_SIZE;
                }
                kfree(cluster_buf);
                return 0; // Found
            }
            
            lfn_count = 0;
        }
        
        cluster = get_next_cluster(fs_data, cluster);
    }
    
    kfree(cluster_buf);
    return -1; // Not found
}

static vnode_t* create_vnode_from_entry(fat32_data_t* fs_data, fat32_dir_entry_t* entry,
                                       const char* name, filesystem_t* fs, uint32_t parent_cluster) {
    vnode_t* vnode = (vnode_t*)kmalloc(sizeof(vnode_t));
    if (!vnode) {
        return NULL;
    }
    
    memset(vnode, 0, sizeof(vnode_t));
    strncpy(vnode->name, name, 255);
    vnode->name[255] = '\0';
    
    uint32_t first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
    
    vnode->inode = first_cluster;
    vnode->type = (entry->attr & FAT32_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    vnode->size = entry->file_size;
    vnode->fs = fs;
    vnode->ops = &fat32_vnode_ops;
    vnode->refcount = 1;
    
    // Setup file-specific data
    fat32_file_data_t* file_data = (fat32_file_data_t*)kmalloc(sizeof(fat32_file_data_t));
    if (file_data) {
        file_data->first_cluster = first_cluster;
        file_data->current_cluster = first_cluster;
        file_data->cluster_offset = 0;
        file_data->file_size = entry->file_size;
        file_data->attributes = entry->attr;
        file_data->parent_cluster = parent_cluster;  // Store parent for updates
        strncpy(file_data->name, name, 255);
        file_data->name[255] = '\0';
        vnode->fs_data = file_data;
    }
    
    // Set default permissions
    vnode->access.owner_id = OWNER_ROOT;
    vnode->access.owner_type = OWNER_ROOT;
    vnode->access.owner_access = (entry->attr & FAT32_ATTR_READ_ONLY) ? 
                                 ACCESS_READ : (ACCESS_READ | ACCESS_WRITE);
    vnode->access.other_access = (entry->attr & FAT32_ATTR_READ_ONLY) ? 
                                 ACCESS_READ : (ACCESS_READ | ACCESS_WRITE);
    
    return vnode;
}


// VFS Operations


static int fat32_vnode_open(vnode_t* node, uint32_t flags) {
    if (!node) {
        return VFS_ERR_INVALID;
    }
    return VFS_OK;
}

static int fat32_vnode_close(vnode_t* node) {
    if (!node || !node->fs || !node->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    // If this is a file, update its directory entry with final size
    if (node->type == VFS_FILE) {
        fat32_data_t* fs_data = (fat32_data_t*)node->fs->fs_data;
        fat32_file_data_t* file_data = (fat32_file_data_t*)node->fs_data;
        
        if (fs_data && file_data && file_data->parent_cluster >= 2) {
            // Create updated entry
            fat32_dir_entry_t updated_entry;
            memset(&updated_entry, 0, sizeof(fat32_dir_entry_t));
            updated_entry.first_cluster_low = (uint16_t)(file_data->first_cluster & 0xFFFF);
            updated_entry.first_cluster_high = (uint16_t)(file_data->first_cluster >> 16);
            updated_entry.file_size = file_data->file_size;
            
            // Update the directory entry
            if (update_dir_entry(fs_data, file_data->parent_cluster, file_data->name, &updated_entry) == 0) {
                // Sync to ensure update is written to disk
                fat32_sync(fs_data);
            }
        }
    }
    
    return VFS_OK;
}

static int fat32_vnode_read(vnode_t* node, void* buffer, uint32_t size, uint32_t offset) {
    if (!node || !buffer || !node->fs_data || !node->fs) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_FILE) {
        return VFS_ERR_ISDIR;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)node->fs->fs_data;
    fat32_file_data_t* file_data = (fat32_file_data_t*)node->fs_data;
    
    if (offset >= file_data->file_size) {
        return 0; // EOF
    }
    
    // Adjust size to not read past EOF
    if (offset + size > file_data->file_size) {
        size = file_data->file_size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint32_t cluster = file_data->first_cluster;
    uint32_t cluster_size = fs_data->bytes_per_cluster;
    
    // Skip to the cluster containing offset
    uint32_t cluster_skip = offset / cluster_size;
    for (uint32_t i = 0; i < cluster_skip && cluster >= 2; i++) {
        cluster = get_next_cluster(fs_data, cluster);
    }
    
    if (cluster < 2 || cluster >= FAT32_CLUSTER_RESERVED) {
        return VFS_ERR_IO;
    }
    
    uint32_t cluster_offset = offset % cluster_size;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) {
        return VFS_ERR_NOSPACE;
    }
    
    while (bytes_read < size && cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED) {
        if (read_cluster(fs_data, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return VFS_ERR_IO;
        }
        
        uint32_t bytes_to_copy = cluster_size - cluster_offset;
        if (bytes_to_copy > size - bytes_read) {
            bytes_to_copy = size - bytes_read;
        }
        
        memcpy((uint8_t*)buffer + bytes_read, cluster_buf + cluster_offset, bytes_to_copy);
        bytes_read += bytes_to_copy;
        cluster_offset = 0;
        
        if (bytes_read < size) {
            cluster = get_next_cluster(fs_data, cluster);
        }
    }
    
    kfree(cluster_buf);
    return (int)bytes_read;
}

static int fat32_vnode_write(vnode_t* node, const void* buffer, uint32_t size, uint32_t offset) {
    if (!node || !buffer || !node->fs_data || !node->fs) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_FILE) {
        return VFS_ERR_ISDIR;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)node->fs->fs_data;
    fat32_file_data_t* file_data = (fat32_file_data_t*)node->fs_data;
    
    uint32_t bytes_written = 0;
    uint32_t cluster = file_data->first_cluster;
    uint32_t cluster_size = fs_data->bytes_per_cluster;
    
    // Handle write to new file (no clusters allocated yet)
    if (cluster < 2) {
        cluster = alloc_cluster(fs_data);
        if (cluster < 2) {
            return VFS_ERR_NOSPACE;
        }
        file_data->first_cluster = cluster;
        file_data->current_cluster = cluster;
    }
    
    // Skip to the cluster containing offset
    uint32_t cluster_skip = offset / cluster_size;
    uint32_t prev_cluster = 0;
    
    for (uint32_t i = 0; i < cluster_skip; i++) {
        prev_cluster = cluster;
        uint32_t next = get_next_cluster(fs_data, cluster);
        
        if (next >= FAT32_CLUSTER_RESERVED) {
            // Need to allocate new cluster
            uint32_t new_cluster = alloc_cluster(fs_data);
            if (new_cluster < 2) {
                return VFS_ERR_NOSPACE;
            }
            set_next_cluster(fs_data, cluster, new_cluster);
            cluster = new_cluster;
        } else {
            cluster = next;
        }
    }
    
    uint32_t cluster_offset = offset % cluster_size;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) {
        return VFS_ERR_NOSPACE;
    }
    
    while (bytes_written < size) {
        // Read existing cluster data (for partial writes)
        if (cluster_offset != 0 || size - bytes_written < cluster_size) {
            if (read_cluster(fs_data, cluster, cluster_buf) != 0) {
                // If read fails, zero the buffer
                memset(cluster_buf, 0, cluster_size);
            }
        }
        
        uint32_t bytes_to_copy = cluster_size - cluster_offset;
        if (bytes_to_copy > size - bytes_written) {
            bytes_to_copy = size - bytes_written;
        }
        
        memcpy(cluster_buf + cluster_offset, (const uint8_t*)buffer + bytes_written, bytes_to_copy);
        
        if (write_cluster(fs_data, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return VFS_ERR_IO;
        }
        
        bytes_written += bytes_to_copy;
        cluster_offset = 0;
        
        if (bytes_written < size) {
            prev_cluster = cluster;
            uint32_t next = get_next_cluster(fs_data, cluster);
            
            if (next >= FAT32_CLUSTER_RESERVED) {
                uint32_t new_cluster = alloc_cluster(fs_data);
                if (new_cluster < 2) {
                    kfree(cluster_buf);
                    // Update file size even if not all bytes written
                    if (offset + bytes_written > file_data->file_size) {
                        file_data->file_size = offset + bytes_written;
                        node->size = file_data->file_size;
                    }
                    return bytes_written;
                }
                set_next_cluster(fs_data, cluster, new_cluster);
                cluster = new_cluster;
            } else {
                cluster = next;
            }
        }
    }
    
    kfree(cluster_buf);
    
    // Update file size if we extended the file
    if (offset + bytes_written > file_data->file_size) {
        file_data->file_size = offset + bytes_written;
        node->size = file_data->file_size;
        
        // Update directory entry with new file size
        // Need to find parent directory and update the entry
        // For now, we'll update it during close or rely on periodic sync
    }
    
    return (int)bytes_written;
}

static vnode_t* fat32_vnode_finddir(vnode_t* node, const char* name) {
    if (!node || !name || !node->fs || !node->fs->fs_data) {
        return NULL;
    }
    
    if (node->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)node->fs->fs_data;
    fat32_file_data_t* file_data = (fat32_file_data_t*)node->fs_data;
    
    fat32_dir_entry_t entry;
    if (find_dir_entry(fs_data, file_data->first_cluster, name, &entry, NULL, NULL) == 0) {
        return create_vnode_from_entry(fs_data, &entry, name, node->fs, file_data->first_cluster);
    }
    
    return NULL;
}

static vnode_t* fat32_vnode_create(vnode_t* parent, const char* name, uint32_t flags) {
    if (!parent || !name || !parent->fs || !parent->fs->fs_data) {
        return NULL;
    }
    
    if (parent->type != VFS_DIRECTORY) {
        return NULL;
    }
    
    // Check if file already exists
    if (fat32_vnode_finddir(parent, name) != NULL) {
        return NULL; // File already exists
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)parent->fs->fs_data;
    fat32_file_data_t* parent_data = (fat32_file_data_t*)parent->fs_data;
    
    // Allocate cluster for new file (optional - can be allocated on first write)
    uint32_t new_cluster = alloc_cluster(fs_data);
    if (new_cluster < 2) {
        return NULL;
    }
    
    // Create directory entry
    fat32_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(fat32_dir_entry_t));
    
    create_short_name(name, new_entry.name);
    new_entry.attr = 0; // Regular file
    new_entry.first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
    new_entry.first_cluster_high = (uint16_t)(new_cluster >> 16);
    new_entry.file_size = 0;
    
    // Add entry to parent directory
    if (add_dir_entry(fs_data, parent_data->first_cluster, name, &new_entry) != 0) {
        // Failed to add entry, free the allocated cluster
        free_cluster_chain(fs_data, new_cluster);
        return NULL;
    }
    
    // Sync to disk immediately to ensure persistence
    fat32_sync(fs_data);
    
    return create_vnode_from_entry(fs_data, &new_entry, name, parent->fs, parent_data->first_cluster);
}

static int fat32_vnode_unlink(vnode_t* parent, const char* name) {
    if (!parent || !name || !parent->fs || !parent->fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    if (parent->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)parent->fs->fs_data;
    fat32_file_data_t* parent_data = (fat32_file_data_t*)parent->fs_data;
    
    fat32_dir_entry_t entry;
    uint32_t entry_sector, entry_offset;
    
    if (find_dir_entry(fs_data, parent_data->first_cluster, name, &entry, 
                      &entry_sector, &entry_offset) != 0) {
        return VFS_ERR_NOTFOUND;
    }
    
    // Don't delete directories (use rmdir instead)
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return VFS_ERR_ISDIR;
    }
    
    // Free cluster chain
    uint32_t first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    if (first_cluster >= 2) {
        free_cluster_chain(fs_data, first_cluster);
    }
    
    // Mark directory entry as deleted
    uint8_t sector_buf[FAT32_SECTOR_SIZE];
    if (read_sector(fs_data, entry_sector, sector_buf) == 0) {
        sector_buf[entry_offset] = 0xE5; // Deleted marker
        write_sector(fs_data, entry_sector, sector_buf);
    }
    
    return VFS_OK;
}

static int fat32_vnode_mkdir(vnode_t* parent, const char* name) {
    if (!parent || !name || !parent->fs || !parent->fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    if (parent->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    // Check if directory already exists
    if (fat32_vnode_finddir(parent, name) != NULL) {
        return VFS_ERR_EXISTS;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)parent->fs->fs_data;
    
    // Allocate cluster for new directory
    uint32_t new_cluster = alloc_cluster(fs_data);
    if (new_cluster < 2) {
        return VFS_ERR_NOSPACE;
    }
    
    // Create . and .. entries
    uint8_t* cluster_buf = (uint8_t*)kmalloc(fs_data->bytes_per_cluster);
    if (!cluster_buf) {
        free_cluster_chain(fs_data, new_cluster);
        return VFS_ERR_NOSPACE;
    }
    
    memset(cluster_buf, 0, fs_data->bytes_per_cluster);
    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
    
    // . entry (self)
    memset(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
    entries[0].first_cluster_high = (uint16_t)(new_cluster >> 16);
    
    // .. entry (parent)
    fat32_file_data_t* parent_data = (fat32_file_data_t*)parent->fs_data;
    memset(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    // For subdirectories of root, parent cluster is root cluster, not 0
    // Cluster 0 would be used if the *current* directory is root and we're making ".." for root itself
    uint32_t parent_cluster = parent_data->first_cluster;
    entries[1].first_cluster_low = (uint16_t)(parent_cluster & 0xFFFF);
    entries[1].first_cluster_high = (uint16_t)(parent_cluster >> 16);
    
    write_cluster(fs_data, new_cluster, cluster_buf);
    kfree(cluster_buf);
    
    // Create directory entry for parent
    fat32_dir_entry_t new_entry;
    memset(&new_entry, 0, sizeof(fat32_dir_entry_t));
    
    create_short_name(name, new_entry.name);
    new_entry.attr = FAT32_ATTR_DIRECTORY;
    new_entry.first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
    new_entry.first_cluster_high = (uint16_t)(new_cluster >> 16);
    new_entry.file_size = 0;
    
    // Add entry to parent directory
    if (add_dir_entry(fs_data, parent_data->first_cluster, name, &new_entry) != 0) {
        // Failed to add entry, free the allocated cluster
        free_cluster_chain(fs_data, new_cluster);
        return VFS_ERR_IO;
    }
    
    // Sync to disk immediately to ensure persistence
    fat32_sync(fs_data);
    
    return VFS_OK;
}

static int fat32_vnode_readdir(vnode_t* node, uint32_t index, dirent_t* dirent) {
    if (!node || !dirent || !node->fs || !node->fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    if (node->type != VFS_DIRECTORY) {
        return VFS_ERR_NOTDIR;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)node->fs->fs_data;
    fat32_file_data_t* file_data = (fat32_file_data_t*)node->fs_data;
    
    uint8_t* cluster_buf = (uint8_t*)kmalloc(fs_data->bytes_per_cluster);
    if (!cluster_buf) {
        return VFS_ERR_NOSPACE;
    }
    
    uint32_t cluster = file_data->first_cluster;
    uint32_t entry_count = 0;
    char lfn_name[FAT32_MAX_FILENAME + 1];
    char lfn_parts[20][14];
    int lfn_count = 0;
    
    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED) {
        if (read_cluster(fs_data, cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return VFS_ERR_IO;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
        uint32_t entries_per_cluster = fs_data->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == 0x00) {
                kfree(cluster_buf);
                return VFS_ERR_NOTFOUND; // End of directory
            }
            
            if (entries[i].name[0] == 0xE5) {
                lfn_count = 0;
                continue; // Deleted entry
            }
            
            if ((entries[i].attr & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
                fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)&entries[i];
                int order = lfn->order & 0x1F;
                if (order > 0 && order <= 20) {
                    parse_lfn_entry(lfn, lfn_parts[order - 1]);
                    if (order > lfn_count) {
                        lfn_count = order;
                    }
                }
                continue;
            }
            
            // Regular entry
            if (entry_count == index) {
                if (lfn_count > 0) {
                    lfn_name[0] = '\0';
                    for (int j = lfn_count - 1; j >= 0; j--) {
                        strcat(lfn_name, lfn_parts[j]);
                    }
                    strncpy(dirent->name, lfn_name, 255);
                } else {
                    parse_short_name(entries[i].name, dirent->name);
                }
                
                dirent->name[255] = '\0';
                uint32_t first_cluster = ((uint32_t)entries[i].first_cluster_high << 16) | 
                                        entries[i].first_cluster_low;
                dirent->inode = first_cluster;
                dirent->type = (entries[i].attr & FAT32_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
                
                kfree(cluster_buf);
                return VFS_OK;
            }
            
            entry_count++;
            lfn_count = 0;
        }
        
        cluster = get_next_cluster(fs_data, cluster);
    }
    
    kfree(cluster_buf);
    return VFS_ERR_NOTFOUND;
}

static int fat32_vnode_stat(vnode_t* node, stat_t* stat) {
    if (!node || !stat) {
        return VFS_ERR_INVALID;
    }
    
    memset(stat, 0, sizeof(stat_t));
    stat->st_ino = node->inode;
    stat->st_mode = node->type;
    stat->st_size = node->size;
    stat->st_blksize = 512;
    stat->st_blocks = (node->size + 511) / 512;
    
    return VFS_OK;
}


// Filesystem Operations

static uint32_t fat32_parse_source_lba(const char* source) {
    if (!source || !*source) {
        return 0;
    }

    const char* lba_pos = strstr(source, "lba=");
    if (!lba_pos) {
        lba_pos = strstr(source, "lba:");
    }
    if (!lba_pos) {
        return 0;
    }

    lba_pos += 4;
    uint32_t lba = 0;
    while (*lba_pos >= '0' && *lba_pos <= '9') {
        lba = (lba * 10U) + (uint32_t)(*lba_pos - '0');
        lba_pos++;
    }

    return lba;
}


static int fat32_mount(filesystem_t* fs, const char* source, uint32_t flags) {
    if (!fs) {
        return VFS_ERR_INVALID;
    }
    
    serial_puts("FAT32: Attempting to mount filesystem...\n");
    
    // Allocate filesystem data
    fat32_data_t* fs_data = (fat32_data_t*)kmalloc(sizeof(fat32_data_t));
    if (!fs_data) {
        serial_puts("FAT32: Failed to allocate fs_data\n");
        return VFS_ERR_NOSPACE;
    }
    
    memset(fs_data, 0, sizeof(fat32_data_t));
    fs_data->start_lba = fat32_parse_source_lba(source);
    
    // Read boot sector
    if (read_sector(fs_data, 0, &fs_data->boot_sector) != 0) {
        serial_puts("FAT32: Failed to read boot sector\n");
        kfree(fs_data);
        return VFS_ERR_IO;
    }
    
    // Verify FAT32 signature (fallback to backup boot sector at +6 if needed).
    if (fs_data->boot_sector.boot_sector_signature != FAT32_SIGNATURE_55AA) {
        fat32_boot_sector_t backup_boot;
        if (read_sector(fs_data, 6, &backup_boot) == 0 &&
            backup_boot.boot_sector_signature == FAT32_SIGNATURE_55AA) {
            memcpy(&fs_data->boot_sector, &backup_boot, sizeof(fat32_boot_sector_t));
            serial_puts("FAT32: Primary boot sector invalid, recovered from backup boot sector\n");
        } else {
            serial_puts("FAT32: Invalid boot sector signature\n");
            kfree(fs_data);
            return VFS_ERR_INVALID;
        }
    }
    
    // Verify it's FAT32
    if (fs_data->boot_sector.fat_size_16 != 0 || fs_data->boot_sector.root_entry_count != 0) {
        serial_puts("FAT32: Not a FAT32 filesystem (appears to be FAT12/FAT16)\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }

    // Validate key BPB fields before using them in arithmetic.
    // This prevents malformed sectors from causing runtime faults.
    if (memcmp(fs_data->boot_sector.fs_type, "FAT32   ", 8) != 0) {
        serial_puts("FAT32: Invalid filesystem type field\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }

    if (fs_data->boot_sector.bytes_per_sector != FAT32_SECTOR_SIZE) {
        serial_puts("FAT32: Unsupported bytes-per-sector value\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }

    uint8_t sectors_per_cluster = fs_data->boot_sector.sectors_per_cluster;
    if (sectors_per_cluster == 0 ||
        (sectors_per_cluster & (sectors_per_cluster - 1)) != 0) {
        serial_puts("FAT32: Invalid sectors-per-cluster\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }

    if (fs_data->boot_sector.num_fats == 0 || fs_data->boot_sector.fat_size_32 == 0) {
        serial_puts("FAT32: Invalid FAT geometry\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }
    
    // Calculate filesystem parameters
    fs_data->bytes_per_cluster = fs_data->boot_sector.bytes_per_sector * 
                                  sectors_per_cluster;
    fs_data->fat_start_sector = fs_data->boot_sector.reserved_sectors;
    fs_data->data_start_sector = fs_data->fat_start_sector + 
                                 (fs_data->boot_sector.num_fats * fs_data->boot_sector.fat_size_32);
    
    uint32_t total_sectors = (fs_data->boot_sector.total_sectors_16 != 0) ?
                             fs_data->boot_sector.total_sectors_16 :
                             fs_data->boot_sector.total_sectors_32;
    if (total_sectors == 0 || total_sectors <= fs_data->data_start_sector) {
        serial_puts("FAT32: Invalid total sector count\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }

    uint32_t data_sectors = total_sectors - fs_data->data_start_sector;
    fs_data->total_clusters = data_sectors / sectors_per_cluster;
    if (fs_data->total_clusters == 0) {
        serial_puts("FAT32: No data clusters available\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }
    
    serial_puts("FAT32: Bytes per cluster: ");
    char buf[32];
    itoa(fs_data->bytes_per_cluster, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("FAT32: Total clusters: ");
    itoa(fs_data->total_clusters, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    // Read FSInfo sector
    if (fs_data->boot_sector.fs_info != 0 && fs_data->boot_sector.fs_info != 0xFFFF) {
        if (read_sector(fs_data, fs_data->boot_sector.fs_info, &fs_data->fsinfo) == 0) {
            if (fs_data->fsinfo.lead_signature == 0x41615252 &&
                fs_data->fsinfo.struct_signature == 0x61417272) {
                serial_puts("FAT32: FSInfo sector loaded\n");
            }
        }
    }
    
    // Load FAT into memory (cache first FAT)
    uint32_t fat_size_bytes = fs_data->boot_sector.fat_size_32 * fs_data->boot_sector.bytes_per_sector;
    if (fat_size_bytes == 0) {
        serial_puts("FAT32: Invalid FAT size in bytes\n");
        kfree(fs_data);
        return VFS_ERR_INVALID;
    }
    fs_data->fat = (uint32_t*)kmalloc(fat_size_bytes);
    if (!fs_data->fat) {
        serial_puts("FAT32: Failed to allocate FAT cache\n");
        kfree(fs_data);
        return VFS_ERR_NOSPACE;
    }
    
    // Read FAT sectors
    serial_puts("FAT32: Loading FAT into memory...\n");
    uint8_t* fat_ptr = (uint8_t*)fs_data->fat;
    for (uint32_t i = 0; i < fs_data->boot_sector.fat_size_32; i++) {
        if (read_sector(fs_data, fs_data->fat_start_sector + i, 
                       fat_ptr + (i * fs_data->boot_sector.bytes_per_sector)) != 0) {
            serial_puts("FAT32: Failed to read FAT sector\n");
            kfree(fs_data->fat);
            kfree(fs_data);
            return VFS_ERR_IO;
        }
    }
    
    fs->fs_data = fs_data;
    serial_puts("FAT32: Mount successful\n");
    
    return VFS_OK;
}

static int fat32_unmount(filesystem_t* fs) {
    if (!fs || !fs->fs_data) {
        return VFS_ERR_INVALID;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)fs->fs_data;
    
    // Write back FAT if dirty
    if (fs_data->fat_cache_dirty && fs_data->fat) {
        serial_puts("FAT32: Writing back FAT cache...\n");
        uint8_t* fat_ptr = (uint8_t*)fs_data->fat;
        for (uint32_t i = 0; i < fs_data->boot_sector.fat_size_32; i++) {
            write_sector(fs_data, fs_data->fat_start_sector + i,
                        fat_ptr + (i * fs_data->boot_sector.bytes_per_sector));
        }
        
        // Write to backup FAT if enabled
        if (!(fs_data->boot_sector.ext_flags & 0x80)) {
            uint32_t backup_fat_start = fs_data->fat_start_sector + fs_data->boot_sector.fat_size_32;
            for (uint32_t i = 0; i < fs_data->boot_sector.fat_size_32; i++) {
                write_sector(fs_data, backup_fat_start + i,
                            fat_ptr + (i * fs_data->boot_sector.bytes_per_sector));
            }
        }
        
        fs_data->fat_cache_dirty = 0;
    }
    
    // Write back FSInfo
    if (fs_data->boot_sector.fs_info != 0 && fs_data->boot_sector.fs_info != 0xFFFF) {
        write_sector(fs_data, fs_data->boot_sector.fs_info, &fs_data->fsinfo);
    }
    
    if (fs_data->fat) {
        kfree(fs_data->fat);
    }
    
    kfree(fs_data);
    fs->fs_data = NULL;
    
    serial_puts("FAT32: Unmount successful\n");
    return VFS_OK;
}

// Sync function to flush cached data to disk
static int fat32_sync(fat32_data_t* fs_data) {
    if (!fs_data) {
        return -1;
    }
    
    // Write back FAT if dirty
    if (fs_data->fat_cache_dirty && fs_data->fat) {
        uint8_t* fat_ptr = (uint8_t*)fs_data->fat;
        for (uint32_t i = 0; i < fs_data->boot_sector.fat_size_32; i++) {
            write_sector(fs_data, fs_data->fat_start_sector + i,
                        fat_ptr + (i * fs_data->boot_sector.bytes_per_sector));
        }
        
        // Write to backup FAT if enabled
        if (!(fs_data->boot_sector.ext_flags & 0x80)) {
            uint32_t backup_fat_start = fs_data->fat_start_sector + fs_data->boot_sector.fat_size_32;
            for (uint32_t i = 0; i < fs_data->boot_sector.fat_size_32; i++) {
                write_sector(fs_data, backup_fat_start + i,
                            fat_ptr + (i * fs_data->boot_sector.bytes_per_sector));
            }
        }
        
        fs_data->fat_cache_dirty = 0;
    }
    
    // Write back FSInfo
    if (fs_data->boot_sector.fs_info != 0 && fs_data->boot_sector.fs_info != 0xFFFF) {
        write_sector(fs_data, fs_data->boot_sector.fs_info, &fs_data->fsinfo);
    }
    
    return 0;
}

static vnode_t* fat32_get_root(filesystem_t* fs) {
    if (!fs || !fs->fs_data) {
        return NULL;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)fs->fs_data;
    
    vnode_t* root = (vnode_t*)kmalloc(sizeof(vnode_t));
    if (!root) {
        return NULL;
    }
    
    memset(root, 0, sizeof(vnode_t));
    strcpy(root->name, "/");
    root->inode = fs_data->boot_sector.root_cluster;
    root->type = VFS_DIRECTORY;
    root->size = 0;
    root->fs = fs;
    root->ops = &fat32_vnode_ops;
    root->refcount = 1;
    
    fat32_file_data_t* file_data = (fat32_file_data_t*)kmalloc(sizeof(fat32_file_data_t));
    if (file_data) {
        file_data->first_cluster = fs_data->boot_sector.root_cluster;
        file_data->current_cluster = fs_data->boot_sector.root_cluster;
        file_data->cluster_offset = 0;
        file_data->file_size = 0;
        file_data->attributes = FAT32_ATTR_DIRECTORY;
        file_data->parent_cluster = 0;  // Root has no parent
        file_data->name[0] = '\0';      // Root has no name
        root->fs_data = file_data;
    }
    
    // Set default permissions
    root->access.owner_id = OWNER_ROOT;
    root->access.owner_type = OWNER_ROOT;
    root->access.owner_access = ACCESS_READ | ACCESS_WRITE;
    root->access.other_access = ACCESS_READ | ACCESS_WRITE;
    
    return root;
}


// Public API


void fat32_init(void) {
    serial_puts("Initializing FAT32 filesystem driver...\n");
    vfs_register_filesystem(&fat32_filesystem);
    serial_puts("FAT32 filesystem driver registered.\n");
}

filesystem_t* fat32_get_fs(void) {
    return &fat32_filesystem;
}

int fat32_format(uint32_t start_lba, uint32_t num_sectors, const char* volume_label) {
    serial_puts("FAT32: Starting format operation...\n");
    
    if (num_sectors < 65536) {
        serial_puts("FAT32: Disk too small for FAT32 (minimum 32MB)\n");
        return -1;
    }
    
    // Calculate filesystem parameters
    uint8_t sectors_per_cluster = 8;  // 4KB clusters for typical disk
    if (num_sectors > 16777216) {     // > 8GB
        sectors_per_cluster = 64;      // 32KB clusters
    } else if (num_sectors > 4194304) { // > 2GB
        sectors_per_cluster = 16;      // 8KB clusters
    }
    
    uint32_t reserved_sectors = 32;
    uint32_t num_fats = 2;
    uint32_t root_cluster = 2;
    
    // Calculate FAT size
    uint32_t total_clusters = (num_sectors - reserved_sectors) / sectors_per_cluster;
    uint32_t fat_size_sectors = (total_clusters * 4 + 511) / 512;  // 4 bytes per FAT entry
    if (fat_size_sectors < 1) fat_size_sectors = 1;
    
    // Adjust for FAT overhead
    uint32_t fat_overhead = num_fats * fat_size_sectors;
    total_clusters = (num_sectors - reserved_sectors - fat_overhead) / sectors_per_cluster;
    fat_size_sectors = (total_clusters * 4 + 511) / 512;
    
    serial_puts("FAT32: Creating boot sector...\n");
    
    // Create boot sector
    fat32_boot_sector_t boot;
    memset(&boot, 0, sizeof(fat32_boot_sector_t));
    
    // Jump instruction
    boot.jump_boot[0] = 0xEB;
    boot.jump_boot[1] = 0x58;
    boot.jump_boot[2] = 0x90;
    
    // OEM name (exactly 8 bytes)
    memcpy(boot.oem_name, "aOSFAT32", 8);
    
    // BPB
    boot.bytes_per_sector = 512;
    boot.sectors_per_cluster = sectors_per_cluster;
    boot.reserved_sectors = reserved_sectors;
    boot.num_fats = num_fats;
    boot.root_entry_count = 0;  // FAT32 uses cluster for root
    boot.total_sectors_16 = 0;  // Use 32-bit field
    boot.media_type = 0xF8;     // Hard disk
    boot.fat_size_16 = 0;       // Use FAT32 field
    boot.sectors_per_track = 63;
    boot.num_heads = 255;
    // When formatting a partitioned disk, this should reflect partition start.
    boot.hidden_sectors = start_lba;
    boot.total_sectors_32 = num_sectors;
    
    // FAT32 extended BPB
    boot.fat_size_32 = fat_size_sectors;
    boot.ext_flags = 0;
    boot.fs_version = 0;
    boot.root_cluster = root_cluster;
    boot.fs_info = 1;           // FSInfo at sector 1
    boot.backup_boot_sector = 6;
    boot.drive_number = 0x80;   // Hard disk
    boot.boot_signature = 0x29; // Extended boot signature
    boot.volume_id = 0x12345678; // Simple volume ID
    
    // Volume label (padded with spaces)
    memset(boot.volume_label, ' ', 11);
    if (volume_label) {
        int len = strlen(volume_label);
        if (len > 11) len = 11;
        memcpy(boot.volume_label, volume_label, len);
    }
    
    // Filesystem type
    memcpy(boot.fs_type, "FAT32   ", 8);
    boot.boot_sector_signature = 0xAA55;
    
    // Write boot sector
    if (ata_write_sectors(start_lba, 1, (const uint8_t*)&boot) != 0) {
        serial_puts("FAT32: Failed to write boot sector\n");
        return -1;
    }
    
    // Write backup boot sector
    if (ata_write_sectors(start_lba + 6, 1, (const uint8_t*)&boot) != 0) {
        serial_puts("FAT32: Failed to write backup boot sector\n");
        return -1;
    }

    // Verify boot sector signatures were persisted correctly.
    {
        uint8_t verify_sector[FAT32_SECTOR_SIZE];
        uint16_t sig;

        if (ata_read_sectors(start_lba, 1, verify_sector) != 0) {
            serial_puts("FAT32: Failed to verify primary boot sector\n");
            return -1;
        }
        sig = (uint16_t)verify_sector[510] | ((uint16_t)verify_sector[511] << 8);
        if (sig != FAT32_SIGNATURE_55AA) {
            serial_puts("FAT32: Primary boot signature verification failed\n");
            return -1;
        }

        if (ata_read_sectors(start_lba + 6, 1, verify_sector) != 0) {
            serial_puts("FAT32: Failed to verify backup boot sector\n");
            return -1;
        }
        sig = (uint16_t)verify_sector[510] | ((uint16_t)verify_sector[511] << 8);
        if (sig != FAT32_SIGNATURE_55AA) {
            serial_puts("FAT32: Backup boot signature verification failed\n");
            return -1;
        }
    }
    
    serial_puts("FAT32: Creating FSInfo sector...\n");
    
    // Create FSInfo sector
    fat32_fsinfo_t fsinfo;
    memset(&fsinfo, 0, sizeof(fat32_fsinfo_t));
    fsinfo.lead_signature = 0x41615252;
    fsinfo.struct_signature = 0x61417272;
    fsinfo.free_clusters = total_clusters - 1; // -1 for root directory
    fsinfo.next_free_cluster = 3; // After root cluster
    fsinfo.trail_signature = 0xAA550000;
    
    // Write FSInfo sector
    if (ata_write_sectors(start_lba + 1, 1, (const uint8_t*)&fsinfo) != 0) {
        serial_puts("FAT32: Failed to write FSInfo sector\n");
        return -1;
    }
    
    serial_puts("FAT32: Initializing FAT tables...\n");
    
    // Initialize FAT
    uint32_t* fat_buffer = (uint32_t*)kmalloc(512);
    if (!fat_buffer) {
        serial_puts("FAT32: Failed to allocate FAT buffer\n");
        return -1;
    }
    
    // First FAT sector with special entries
    memset(fat_buffer, 0, 512);
    fat_buffer[0] = 0x0FFFFFF8;  // Media type
    fat_buffer[1] = 0x0FFFFFFF;  // End of chain (reserved)
    fat_buffer[2] = 0x0FFFFFFF;  // Root directory (end of chain)
    
    // Write first sector of both FATs
    uint32_t fat1_start = start_lba + reserved_sectors;
    uint32_t fat2_start = fat1_start + fat_size_sectors;
    
    if (ata_write_sectors(fat1_start, 1, (const uint8_t*)fat_buffer) != 0 ||
        ata_write_sectors(fat2_start, 1, (const uint8_t*)fat_buffer) != 0) {
        kfree(fat_buffer);
        serial_puts("FAT32: Failed to write FAT\n");
        return -1;
    }
    
    // Zero out rest of FAT
    memset(fat_buffer, 0, 512);
    for (uint32_t i = 1; i < fat_size_sectors; i++) {
        if (ata_write_sectors(fat1_start + i, 1, (const uint8_t*)fat_buffer) != 0 ||
            ata_write_sectors(fat2_start + i, 1, (const uint8_t*)fat_buffer) != 0) {
            kfree(fat_buffer);
            serial_puts("FAT32: Failed to write FAT\n");
            return -1;
        }
    }
    
    kfree(fat_buffer);
    
    serial_puts("FAT32: Creating root directory...\n");
    
    // Create root directory cluster (zero it out)
    uint8_t* cluster_buffer = (uint8_t*)kmalloc(sectors_per_cluster * 512);
    if (!cluster_buffer) {
        serial_puts("FAT32: Failed to allocate cluster buffer\n");
        return -1;
    }
    
    memset(cluster_buffer, 0, sectors_per_cluster * 512);
    
    // Write root directory cluster
    uint32_t data_start = fat2_start + fat_size_sectors;
    uint32_t root_sector = data_start + (root_cluster - 2) * sectors_per_cluster;
    
    for (uint32_t i = 0; i < sectors_per_cluster; i++) {
        if (ata_write_sectors(root_sector + i, 1, cluster_buffer + (i * 512)) != 0) {
            kfree(cluster_buffer);
            serial_puts("FAT32: Failed to write root directory\n");
            return -1;
        }
    }
    
    kfree(cluster_buffer);
    
    serial_puts("FAT32: Format complete\n");
    return 0;
}

int fat32_get_stats(fat32_boot_sector_t* stats) {
    if (!fat32_filesystem.fs_data || !stats) {
        return -1;
    }
    
    fat32_data_t* fs_data = (fat32_data_t*)fat32_filesystem.fs_data;
    memcpy(stats, &fs_data->boot_sector, sizeof(fat32_boot_sector_t));
    return 0;
}
