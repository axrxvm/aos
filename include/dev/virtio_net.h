/*
 * === AOS HEADER BEGIN ===
 * include/dev/virtio_net.h
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


#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include <stdint.h>
#include <net/net.h>

// VirtIO PCI IDs
#define VIRTIO_PCI_VENDOR_ID             0x1AF4
#define VIRTIO_PCI_DEVICE_ID_NET_LEGACY  0x1000
#define VIRTIO_PCI_DEVICE_ID_NET_MODERN  0x1041

// Driver entry points
int virtio_net_init(void);
int virtio_net_transmit(const uint8_t* data, uint32_t len);
void virtio_net_handle_interrupt(void);
net_interface_t* virtio_net_get_interface(void);

#endif // VIRTIO_NET_H