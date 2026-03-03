/*
 * === AOS HEADER BEGIN ===
 * include/fs/ramfs.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

/*
 * DEVELOPER_NOTE_BLOCK
 * Module Overview:
 * - This file is part of the aOS production kernel/userspace codebase.
 * - Review public symbols in this unit to understand contracts with adjacent modules.
 * - Keep behavior-focused comments near non-obvious invariants, state transitions, and safety checks.
 * - Avoid changing ABI/data-layout assumptions without updating dependent modules.
 */


#ifndef RAMFS_H
#define RAMFS_H

#include <fs/vfs.h>

// Initialize ramfs filesystem driver
void ramfs_init(void);

// Get ramfs filesystem type
filesystem_t* ramfs_get_fs(void);

#endif // RAMFS_H
