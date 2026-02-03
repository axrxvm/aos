/*
 * === AOS HEADER BEGIN ===
 * ./include/fs/procfs.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef PROCFS_H
#define PROCFS_H

#include <fs/vfs.h>

void procfs_init(void);
filesystem_t* procfs_get_fs(void);

#endif // PROCFS_H
