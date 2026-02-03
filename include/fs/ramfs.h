/*
 * === AOS HEADER BEGIN ===
 * ./include/fs/ramfs.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef RAMFS_H
#define RAMFS_H

#include <fs/vfs.h>

// Initialize ramfs filesystem driver
void ramfs_init(void);

// Get ramfs filesystem type
filesystem_t* ramfs_get_fs(void);

#endif // RAMFS_H
