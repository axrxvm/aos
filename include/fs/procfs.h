/*
 * === AOS HEADER BEGIN ===
 * include/fs/procfs.h
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


#ifndef PROCFS_H
#define PROCFS_H

#include <fs/vfs.h>

void procfs_init(void);
filesystem_t* procfs_get_fs(void);

#endif // PROCFS_H
