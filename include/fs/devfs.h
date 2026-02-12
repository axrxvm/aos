/*
 * === AOS HEADER BEGIN ===
 * include/fs/devfs.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#ifndef DEVFS_H
#define DEVFS_H

#include <fs/vfs.h>

void devfs_init(void);
filesystem_t* devfs_get_fs(void);

#endif // DEVFS_H
