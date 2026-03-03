/*
 * === AOS HEADER BEGIN ===
 * include/net/loopback.h
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


#ifndef LOOPBACK_H
#define LOOPBACK_H

#include <net/net.h>

// Loopback initialization
void loopback_init(void);

// Get loopback interface
net_interface_t* loopback_get_interface(void);

// Loopback receive (called by loopback_transmit)
int loopback_receive(net_interface_t* iface, net_packet_t* packet);

#endif // LOOPBACK_H
