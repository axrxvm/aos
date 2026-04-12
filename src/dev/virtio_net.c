/*
 * === AOS HEADER BEGIN ===
 * src/dev/virtio_net.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/**
 * Legacy VirtIO-Net PCI Driver
 */

#include <dev/virtio_net.h>
#include <dev/pci.h>
#include <net/net.h>
#include <net/ethernet.h>
#include <net/dhcp.h>
#include <arch/paging.h>
#include <io.h>
#include <serial.h>
#include <string.h>
#include <vmm.h>
#include <stdlib.h>

/*
 * VirtIO-Net NIC driver (legacy I/O BAR interface).
 *
 * Implements queue setup and polling-based RX/TX for QEMU virtio-net-pci
 * devices exposing the legacy register layout.
 */

// Legacy VirtIO PCI register offsets (I/O BAR)
#define VIRTIO_PCI_REG_DEVICE_FEATURES   0x00
#define VIRTIO_PCI_REG_GUEST_FEATURES    0x04
#define VIRTIO_PCI_REG_QUEUE_ADDRESS     0x08
#define VIRTIO_PCI_REG_QUEUE_SIZE        0x0C
#define VIRTIO_PCI_REG_QUEUE_SELECT      0x0E
#define VIRTIO_PCI_REG_QUEUE_NOTIFY      0x10
#define VIRTIO_PCI_REG_DEVICE_STATUS     0x12
#define VIRTIO_PCI_REG_ISR_STATUS        0x13
#define VIRTIO_PCI_REG_DEVICE_CONFIG     0x14

// VirtIO status bits (legacy subset)
#define VIRTIO_STATUS_ACKNOWLEDGE        0x01
#define VIRTIO_STATUS_DRIVER             0x02
#define VIRTIO_STATUS_DRIVER_OK          0x04
#define VIRTIO_STATUS_FAILED             0x80

// VirtIO-Net feature bits
#define VIRTIO_NET_F_MAC                 5

// Queue indices for virtio-net
#define VIRTIO_NET_QUEUE_RX              0
#define VIRTIO_NET_QUEUE_TX              1

// Queue constraints and sizing
#define VIRTIO_QUEUE_ALIGN               4096
#define VIRTIO_QUEUE_MAX_ENTRIES         1024
#define VIRTIO_QUEUE_MAX_MEM             32768

// Buffers
#define VIRTIO_NET_RX_DESC_TARGET        64
#define VIRTIO_NET_RX_BUFFER_SIZE        2048
#define VIRTIO_NET_TX_BUFFER_SIZE        2048

// Keep boot networking responsive: avoid long DHCP blocking during startup.
#define VIRTIO_NET_BOOT_DHCP_TIMEOUT_TICKS 100

// Virtqueue descriptor flags
#define VIRTQ_DESC_F_NEXT                1
#define VIRTQ_DESC_F_WRITE               2

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed)) virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} __attribute__((packed)) virtq_used_t;

typedef struct {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

typedef struct {
    uint16_t size;
    uint8_t* mem;
    virtq_desc_t* desc;
    virtq_avail_t* avail;
    virtq_used_t* used;
    uint16_t avail_idx_shadow;
    uint16_t last_used_idx;
} virtq_state_t;

// Device state
static uint16_t io_base = 0;
static net_interface_t* virtio_iface = NULL;

static virtq_state_t rxq;
static virtq_state_t txq;

static uint8_t rx_queue_mem[VIRTIO_QUEUE_MAX_MEM] __attribute__((aligned(VIRTIO_QUEUE_ALIGN)));
static uint8_t tx_queue_mem[VIRTIO_QUEUE_MAX_MEM] __attribute__((aligned(VIRTIO_QUEUE_ALIGN)));

static uint8_t* rx_buffers[VIRTIO_QUEUE_MAX_ENTRIES];
static uint16_t rx_buffer_size = (uint16_t)(sizeof(virtio_net_hdr_t) + VIRTIO_NET_RX_BUFFER_SIZE);
static uint8_t rx_buffer_pool[VIRTIO_NET_RX_DESC_TARGET][sizeof(virtio_net_hdr_t) + VIRTIO_NET_RX_BUFFER_SIZE] __attribute__((aligned(16)));

static uint8_t tx_buffer[sizeof(virtio_net_hdr_t) + VIRTIO_NET_TX_BUFFER_SIZE] __attribute__((aligned(16)));
static volatile int tx_inflight = 0;

// Statistics
static uint32_t tx_packets = 0;
static uint32_t rx_packets = 0;

static inline uint32_t align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static inline uintptr_t virtio_dma_addr(const void* ptr) {
    /* Kernel and early-heap allocations are identity-mapped in current boot flow. */
    return (uintptr_t)ptr;
}

static inline uint8_t virtio_read_status(void) {
    return inb(io_base + VIRTIO_PCI_REG_DEVICE_STATUS);
}

static inline void virtio_write_status(uint8_t status) {
    outb(io_base + VIRTIO_PCI_REG_DEVICE_STATUS, status);
}

static uint32_t virtq_required_bytes(uint16_t qsize) {
    uint32_t desc_bytes = (uint32_t)sizeof(virtq_desc_t) * qsize;
    uint32_t avail_bytes = (uint32_t)sizeof(uint16_t) * (uint32_t)(3 + qsize);
    uint32_t used_offset = align_up_u32(desc_bytes + avail_bytes, VIRTIO_QUEUE_ALIGN);
    uint32_t used_bytes = (uint32_t)sizeof(uint16_t) * 3U + (uint32_t)sizeof(virtq_used_elem_t) * qsize;
    return used_offset + used_bytes;
}

static int virtq_setup(uint16_t queue_index, virtq_state_t* q, uint8_t* queue_mem, uint32_t queue_mem_size) {
    if (!q || !queue_mem) {
        return -1;
    }

    outw(io_base + VIRTIO_PCI_REG_QUEUE_SELECT, queue_index);
    uint16_t qsize = inw(io_base + VIRTIO_PCI_REG_QUEUE_SIZE);
    if (qsize == 0 || qsize > VIRTIO_QUEUE_MAX_ENTRIES) {
        return -1;
    }

    uint32_t needed = virtq_required_bytes(qsize);
    if (needed > queue_mem_size) {
        return -1;
    }

    memset(queue_mem, 0, queue_mem_size);

    uint32_t desc_bytes = (uint32_t)sizeof(virtq_desc_t) * qsize;
    uint32_t avail_bytes = (uint32_t)sizeof(uint16_t) * (uint32_t)(3 + qsize);
    uint32_t used_offset = align_up_u32(desc_bytes + avail_bytes, VIRTIO_QUEUE_ALIGN);

    q->size = qsize;
    q->mem = queue_mem;
    q->desc = (virtq_desc_t*)(void*)queue_mem;
    q->avail = (virtq_avail_t*)(void*)(queue_mem + desc_bytes);
    q->used = (virtq_used_t*)(void*)(queue_mem + used_offset);
    q->avail_idx_shadow = 0;
    q->last_used_idx = 0;

    uintptr_t q_phys = virtio_dma_addr(queue_mem);
    if ((q_phys & (VIRTIO_QUEUE_ALIGN - 1U)) != 0) {
        return -1;
    }
    uint32_t q_pfn = (uint32_t)(q_phys >> 12);
    outl(io_base + VIRTIO_PCI_REG_QUEUE_ADDRESS, q_pfn);

    return 0;
}

static void virtio_tx_reclaim(void) {
    while (txq.last_used_idx != txq.used->idx) {
        uint16_t ring_idx = (uint16_t)(txq.last_used_idx % txq.size);
        virtq_used_elem_t* elem = &txq.used->ring[ring_idx];
        if (elem->id == 0) {
            tx_inflight = 0;
            tx_packets++;
        }
        txq.last_used_idx++;
    }
}

static void virtio_rx_process(void) {
    uint16_t recycled = 0;

    while (rxq.last_used_idx != rxq.used->idx) {
        uint16_t used_ring_idx = (uint16_t)(rxq.last_used_idx % rxq.size);
        virtq_used_elem_t* elem = &rxq.used->ring[used_ring_idx];
        uint32_t id = elem->id;
        uint32_t total_len = elem->len;

        if (id < rxq.size && rx_buffers[id]) {
            if (total_len > sizeof(virtio_net_hdr_t) && total_len <= rx_buffer_size && virtio_iface) {
                net_packet_t packet;
                packet.data = rx_buffers[id] + sizeof(virtio_net_hdr_t);
                packet.len = total_len - sizeof(virtio_net_hdr_t);
                packet.capacity = VIRTIO_NET_RX_BUFFER_SIZE;

                eth_receive(virtio_iface, &packet);
                rx_packets++;
            }

            rxq.desc[id].addr = (uint64_t)virtio_dma_addr(rx_buffers[id]);
            rxq.desc[id].len = rx_buffer_size;
            rxq.desc[id].flags = VIRTQ_DESC_F_WRITE;
            rxq.desc[id].next = 0;

            uint16_t avail_slot = (uint16_t)(rxq.avail_idx_shadow % rxq.size);
            rxq.avail->ring[avail_slot] = (uint16_t)id;
            rxq.avail_idx_shadow++;
            recycled++;
        }

        rxq.last_used_idx++;
    }

    if (recycled) {
        __asm__ __volatile__("" ::: "memory");
        rxq.avail->idx = rxq.avail_idx_shadow;
        outw(io_base + VIRTIO_PCI_REG_QUEUE_NOTIFY, VIRTIO_NET_QUEUE_RX);
    }
}

static int virtio_iface_transmit(net_interface_t* iface, net_packet_t* packet) {
    (void)iface;

    if (!packet || !packet->data || packet->len == 0) {
        return -1;
    }

    return virtio_net_transmit(packet->data, packet->len);
}

static int virtio_iface_receive(net_interface_t* iface, net_packet_t* packet) {
    (void)iface;
    (void)packet;
    return 0;
}

static pci_device_t* virtio_find_device(void) {
    pci_device_t* dev = pci_find_device(VIRTIO_PCI_VENDOR_ID, VIRTIO_PCI_DEVICE_ID_NET_LEGACY);
    if (dev) {
        return dev;
    }

    dev = pci_find_device(VIRTIO_PCI_VENDOR_ID, VIRTIO_PCI_DEVICE_ID_NET_MODERN);
    return dev;
}

int virtio_net_transmit(const uint8_t* data, uint32_t len) {
    if (!io_base || !data || len == 0 || len > VIRTIO_NET_TX_BUFFER_SIZE) {
        return -1;
    }

    virtio_tx_reclaim();

    if (tx_inflight) {
        int timeout = 100000;
        while (tx_inflight && timeout-- > 0) {
            virtio_tx_reclaim();
            __asm__ __volatile__("pause" ::: "memory");
        }
        if (tx_inflight) {
            serial_puts("virtio-net: TX timeout waiting for free descriptor\n");
            return -1;
        }
    }

    memset(tx_buffer, 0, sizeof(virtio_net_hdr_t));
    memcpy(tx_buffer + sizeof(virtio_net_hdr_t), data, len);

    txq.desc[0].addr = (uint64_t)virtio_dma_addr(tx_buffer);
    txq.desc[0].len = (uint32_t)(sizeof(virtio_net_hdr_t) + len);
    txq.desc[0].flags = 0;
    txq.desc[0].next = 0;

    uint16_t avail_slot = (uint16_t)(txq.avail_idx_shadow % txq.size);
    txq.avail->ring[avail_slot] = 0;
    txq.avail_idx_shadow++;

    __asm__ __volatile__("" ::: "memory");
    txq.avail->idx = txq.avail_idx_shadow;
    tx_inflight = 1;

    outw(io_base + VIRTIO_PCI_REG_QUEUE_NOTIFY, VIRTIO_NET_QUEUE_TX);

    int timeout = 100000;
    while (tx_inflight && timeout-- > 0) {
        virtio_tx_reclaim();
        __asm__ __volatile__("pause" ::: "memory");
    }

    if (tx_inflight) {
        serial_puts("virtio-net: TX completion timeout\n");
        return -1;
    }

    return 0;
}

void virtio_net_handle_interrupt(void) {
    if (!io_base) {
        return;
    }

    // Reading ISR acknowledges pending interrupts in legacy interface.
    (void)inb(io_base + VIRTIO_PCI_REG_ISR_STATUS);

    virtio_tx_reclaim();
    virtio_rx_process();
}

int virtio_net_init(void) {
    serial_puts("virtio-net: Initializing...\n");

    if (virtio_iface) {
        serial_puts("virtio-net: Already initialized\n");
        return 0;
    }

    pci_device_t* dev = virtio_find_device();
    if (!dev) {
        serial_puts("virtio-net: Device not found\n");
        return -1;
    }

    uint16_t command = pci_read_config_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_write_config_word(dev->bus, dev->device, dev->function, PCI_COMMAND, command);

    uint32_t bar0 = dev->bar[0];
    if ((bar0 & 0x1U) == 0) {
        serial_puts("virtio-net: BAR0 is MMIO-only (legacy I/O mode expected)\n");
        return -1;
    }

    io_base = (uint16_t)(bar0 & 0xFFFC);

    serial_puts("virtio-net: I/O base at 0x");
    char io_hex[16];
    itoa(io_base, io_hex, 16);
    serial_puts(io_hex);
    serial_puts("\n");

    virtio_write_status(0);
    virtio_write_status(VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_write_status(VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    uint32_t host_features = inl(io_base + VIRTIO_PCI_REG_DEVICE_FEATURES);
    uint32_t guest_features = 0;
    if (host_features & (1U << VIRTIO_NET_F_MAC)) {
        guest_features |= (1U << VIRTIO_NET_F_MAC);
    }
    outl(io_base + VIRTIO_PCI_REG_GUEST_FEATURES, guest_features);

    if (virtq_setup(VIRTIO_NET_QUEUE_RX, &rxq, rx_queue_mem, sizeof(rx_queue_mem)) != 0) {
        serial_puts("virtio-net: Failed to setup RX queue\n");
        virtio_write_status(virtio_read_status() | VIRTIO_STATUS_FAILED);
        return -1;
    }

    if (virtq_setup(VIRTIO_NET_QUEUE_TX, &txq, tx_queue_mem, sizeof(tx_queue_mem)) != 0) {
        serial_puts("virtio-net: Failed to setup TX queue\n");
        virtio_write_status(virtio_read_status() | VIRTIO_STATUS_FAILED);
        return -1;
    }

    if (txq.size == 0 || rxq.size == 0) {
        serial_puts("virtio-net: Invalid queue sizes\n");
        virtio_write_status(virtio_read_status() | VIRTIO_STATUS_FAILED);
        return -1;
    }

    uint16_t rx_desc_count = rxq.size;
    if (rx_desc_count > VIRTIO_NET_RX_DESC_TARGET) {
        rx_desc_count = VIRTIO_NET_RX_DESC_TARGET;
    }

    for (uint16_t i = 0; i < VIRTIO_QUEUE_MAX_ENTRIES; i++) {
        rx_buffers[i] = NULL;
    }

    for (uint16_t i = 0; i < rx_desc_count; i++) {
        rx_buffers[i] = rx_buffer_pool[i];
        memset(rx_buffers[i], 0, rx_buffer_size);

        rxq.desc[i].addr = (uint64_t)virtio_dma_addr(rx_buffers[i]);
        rxq.desc[i].len = rx_buffer_size;
        rxq.desc[i].flags = VIRTQ_DESC_F_WRITE;
        rxq.desc[i].next = 0;

        rxq.avail->ring[i] = i;
    }

    rxq.avail_idx_shadow = rx_desc_count;
    rxq.avail->idx = rxq.avail_idx_shadow;
    outw(io_base + VIRTIO_PCI_REG_QUEUE_NOTIFY, VIRTIO_NET_QUEUE_RX);

    txq.desc[0].addr = (uint64_t)virtio_dma_addr(tx_buffer);
    txq.desc[0].len = 0;
    txq.desc[0].flags = 0;
    txq.desc[0].next = 0;
    txq.avail_idx_shadow = 0;
    tx_inflight = 0;

    virtio_write_status(virtio_read_status() | VIRTIO_STATUS_DRIVER_OK);

    char ifname[16] = "eth2";
    if (!net_interface_get("eth0")) {
        strcpy(ifname, "eth0");
    } else if (!net_interface_get("eth1")) {
        strcpy(ifname, "eth1");
    }

    virtio_iface = net_interface_register(ifname);
    if (!virtio_iface) {
        serial_puts("virtio-net: Failed to register interface\n");
        virtio_write_status(virtio_read_status() | VIRTIO_STATUS_FAILED);
        return -1;
    }

    if (guest_features & (1U << VIRTIO_NET_F_MAC)) {
        for (int i = 0; i < MAC_ADDR_LEN; i++) {
            virtio_iface->mac_addr.addr[i] = inb(io_base + VIRTIO_PCI_REG_DEVICE_CONFIG + i);
        }
    } else {
        virtio_iface->mac_addr.addr[0] = 0x02;
        virtio_iface->mac_addr.addr[1] = 0xA0;
        virtio_iface->mac_addr.addr[2] = 0x53;
        virtio_iface->mac_addr.addr[3] = dev->bus;
        virtio_iface->mac_addr.addr[4] = dev->device;
        virtio_iface->mac_addr.addr[5] = dev->function;
    }

    virtio_iface->flags = IFF_BROADCAST;
    virtio_iface->mtu = MTU_SIZE;
    virtio_iface->ip_addr = 0;
    virtio_iface->netmask = 0;
    virtio_iface->gateway = 0;
    virtio_iface->transmit = virtio_iface_transmit;
    virtio_iface->receive = virtio_iface_receive;

    net_interface_up(virtio_iface);

    serial_puts("virtio-net: MAC ");
    char mac_str[20];
    mac_to_string(&virtio_iface->mac_addr, mac_str);
    serial_puts(mac_str);
    serial_puts("\n");

    if (dhcp_discover_timed(virtio_iface,
                            VIRTIO_NET_BOOT_DHCP_TIMEOUT_TICKS,
                            VIRTIO_NET_BOOT_DHCP_TIMEOUT_TICKS) == 0) {
        dhcp_config_t* config = dhcp_get_config();
        dhcp_configure_interface(virtio_iface, config);
    } else {
        virtio_iface->ip_addr = 0xA9FE0000 |
                                ((uint32_t)(virtio_iface->mac_addr.addr[4]) << 8) |
                                ((uint32_t)(virtio_iface->mac_addr.addr[5]));
        virtio_iface->netmask = 0xFFFF0000;
    }

    serial_puts("virtio-net: Initialization complete\n");
    return 0;
}

net_interface_t* virtio_net_get_interface(void) {
    return virtio_iface;
}