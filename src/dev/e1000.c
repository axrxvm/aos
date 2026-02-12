/*
 * === AOS HEADER BEGIN ===
 * src/dev/e1000.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/**
 * Intel e1000 (82540EM) Network Driver
 */

#include <dev/e1000.h>
#include <dev/pci.h>
#include <net/net.h>
#include <net/ethernet.h>
#include <net/arp.h>
#include <net/dhcp.h>
#include <arch/i386/paging.h>
#include <io.h>
#include <serial.h>
#include <string.h>
#include <vmm.h>
#include <stdlib.h>

// E1000 Device State
static uint8_t* mmio_base = NULL;
static net_interface_t* e1000_iface = NULL;

// Descriptor Rings (16-byte aligned for DMA)
static e1000_rx_desc_t* rx_descs = NULL;
static e1000_tx_desc_t* tx_descs = NULL;

// Buffer Pools
static uint8_t* rx_buffers[E1000_NUM_RX_DESC];
static uint8_t* tx_buffers[E1000_NUM_TX_DESC];

// Ring Indices
static volatile uint32_t rx_head = 0;
static volatile uint32_t tx_tail = 0;

// Statistics
static uint32_t tx_packets = 0;
static uint32_t rx_packets = 0;


// MMIO Register Access


static inline uint32_t e1000_read_reg(uint32_t reg) {
    return *((volatile uint32_t*)(mmio_base + reg));
}

static inline void e1000_write_reg(uint32_t reg, uint32_t value) {
    *((volatile uint32_t*)(mmio_base + reg)) = value;
    // Memory barrier to ensure write completes
    __asm__ __volatile__("" ::: "memory");
}


// EEPROM Access


static uint16_t e1000_read_eeprom(uint8_t addr) {
    e1000_write_reg(E1000_REG_EERD, ((uint32_t)addr << 8) | 1);
    
    uint32_t tmp;
    int timeout = 10000;
    while (!((tmp = e1000_read_reg(E1000_REG_EERD)) & (1 << 4)) && timeout-- > 0);
    
    return (uint16_t)((tmp >> 16) & 0xFFFF);
}

static void e1000_read_mac_address(mac_addr_t* mac) {
    uint16_t mac_words[3];
    
    for (int i = 0; i < 3; i++) {
        mac_words[i] = e1000_read_eeprom(i);
    }
    
    mac->addr[0] = mac_words[0] & 0xFF;
    mac->addr[1] = (mac_words[0] >> 8) & 0xFF;
    mac->addr[2] = mac_words[1] & 0xFF;
    mac->addr[3] = (mac_words[1] >> 8) & 0xFF;
    mac->addr[4] = mac_words[2] & 0xFF;
    mac->addr[5] = (mac_words[2] >> 8) & 0xFF;
}


// Descriptor Ring Initialization


static void e1000_init_rx(void) {
    // Allocate descriptor ring with 16-byte alignment for DMA
    uint32_t desc_size = sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC;
    uint8_t* raw_desc = (uint8_t*)kmalloc(desc_size + 16);
    rx_descs = (e1000_rx_desc_t*)(((uintptr_t)raw_desc + 15) & ~15);
    memset(rx_descs, 0, desc_size);
    
    // Allocate buffers with 16-byte alignment
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        uint8_t* raw_buf = (uint8_t*)kmalloc(E1000_RX_BUFFER_SIZE + 16);
        rx_buffers[i] = (uint8_t*)(((uintptr_t)raw_buf + 15) & ~15);
        rx_descs[i].addr = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_descs[i].status = 0;
    }
    
    // Configure hardware
    uint32_t rx_base = (uint32_t)(uintptr_t)rx_descs;
    e1000_write_reg(E1000_REG_RDBAL, rx_base);
    e1000_write_reg(E1000_REG_RDBAH, 0);
    e1000_write_reg(E1000_REG_RDLEN, desc_size);
    
    // Hardware starts at head=0, tail points to last available descriptor
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    rx_head = 0;
    
    // Enable receiver with promiscuous mode for DHCP
    uint32_t rctl = E1000_RCTL_EN |        // Receiver Enable
                    E1000_RCTL_UPE |       // Unicast Promiscuous
                    E1000_RCTL_MPE |       // Multicast Promiscuous  
                    E1000_RCTL_BAM |       // Broadcast Accept Mode
                    E1000_RCTL_BSIZE_2K |  // Buffer Size 2048
                    E1000_RCTL_SECRC;      // Strip Ethernet CRC
    e1000_write_reg(E1000_REG_RCTL, rctl);
}

static void e1000_init_tx(void) {
    // Allocate descriptor ring with 16-byte alignment
    uint32_t desc_size = sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC;
    uint8_t* raw_desc = (uint8_t*)kmalloc(desc_size + 16);
    tx_descs = (e1000_tx_desc_t*)(((uintptr_t)raw_desc + 15) & ~15);
    memset(tx_descs, 0, desc_size);
    
    // Allocate buffers with 16-byte alignment
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        uint8_t* raw_buf = (uint8_t*)kmalloc(E1000_TX_BUFFER_SIZE + 16);
        tx_buffers[i] = (uint8_t*)(((uintptr_t)raw_buf + 15) & ~15);
        tx_descs[i].addr = (uint64_t)(uintptr_t)tx_buffers[i];
        tx_descs[i].status = E1000_TXD_STAT_DD;  // Mark as done initially
        tx_descs[i].cmd = 0;
    }
    
    // Configure hardware
    uint32_t tx_base = (uint32_t)(uintptr_t)tx_descs;
    e1000_write_reg(E1000_REG_TDBAL, tx_base);
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, desc_size);
    
    // Both head and tail start at 0 (empty ring)
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    tx_tail = 0;
    
    // Set transmit IPG (Inter-Packet Gap)
    e1000_write_reg(E1000_REG_TIPG, 0x00702008);
    
    // Enable transmitter with collision parameters
    uint32_t tctl = E1000_TCTL_EN |      // Transmit Enable
                    E1000_TCTL_PSP |     // Pad Short Packets
                    (15 << 4) |          // Collision Threshold
                    (64 << 12);          // Collision Distance
    e1000_write_reg(E1000_REG_TCTL, tctl);
}


// Packet Transmission


int e1000_transmit(const uint8_t* data, uint32_t len) {
    if (!mmio_base || !data || len == 0 || len > E1000_TX_BUFFER_SIZE) {
        return -1;
    }
    
    // Get current descriptor
    uint32_t desc_idx = tx_tail;
    e1000_tx_desc_t* desc = &tx_descs[desc_idx];
    
    // Wait for descriptor to be available (if previously used)
    if (desc->cmd != 0) {
        int timeout = 100000;
        while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0) {
            __asm__ __volatile__("pause" ::: "memory");
        }
        if (timeout <= 0) {
            serial_puts("e1000: TX timeout\n");
            return -1;
        }
    }
    
    // Copy data to DMA buffer
    memcpy(tx_buffers[desc_idx], data, len);
    
    // Setup descriptor
    desc->length = len;
    desc->cmd = E1000_TXD_CMD_EOP |    // End of Packet
                E1000_TXD_CMD_IFCS |   // Insert FCS/CRC
                E1000_TXD_CMD_RS;      // Report Status
    desc->status = 0;
    
    // Memory barrier before updating tail
    __asm__ __volatile__("mfence" ::: "memory");
    
    // Advance tail to submit packet
    tx_tail = (tx_tail + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_REG_TDT, tx_tail);
    
    // Wait for transmission to complete
    int timeout = 100000;
    while (!(desc->status & E1000_TXD_STAT_DD) && timeout-- > 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }
    
    tx_packets++;
    

    
    return (desc->status & E1000_TXD_STAT_DD) ? 0 : -1;
}


// Packet Reception


static void e1000_receive(void) {
    // Read hardware head pointer
    uint32_t hw_head = e1000_read_reg(E1000_REG_RDH);
    
    // Process all available packets
    while (rx_head != hw_head) {
        e1000_rx_desc_t* desc = &rx_descs[rx_head];
        
        // Memory barrier to ensure we read fresh data
        __asm__ __volatile__("lfence" ::: "memory");
        
        uint16_t length = desc->length;
        
        rx_packets++;
        
        // Process valid packets
        if (length > 0 && length <= E1000_RX_BUFFER_SIZE && e1000_iface) {
            net_packet_t packet;
            packet.data = rx_buffers[rx_head];
            packet.len = length;
            packet.capacity = E1000_RX_BUFFER_SIZE;
            
            // Process through Ethernet layer
            eth_receive(e1000_iface, &packet);
        }
        
        // Reset descriptor for reuse
        desc->status = 0;
        desc->length = 0;
        desc->errors = 0;
        
        // Return descriptor to hardware
        uint32_t old_head = rx_head;
        rx_head = (rx_head + 1) % E1000_NUM_RX_DESC;
        e1000_write_reg(E1000_REG_RDT, old_head);
    }
}

void e1000_handle_interrupt(void) {
    if (!mmio_base) return;
    
    // Clear any pending interrupts
    e1000_read_reg(E1000_REG_ICR);
    
    // Process received packets
    e1000_receive();
}


// Network Interface Functions


static int e1000_iface_transmit(net_interface_t* iface, net_packet_t* packet) {
    (void)iface;
    
    if (!packet || !packet->data || packet->len == 0) {
        return -1;
    }
    
    return e1000_transmit(packet->data, packet->len);
}

static int e1000_iface_receive(net_interface_t* iface, net_packet_t* packet) {
    (void)iface;
    (void)packet;
    return 0;  // Handled by polling
}


// Driver Initialization


int e1000_init(void) {
    serial_puts("e1000: Initializing...\n");
    
    // Find device on PCI bus
    pci_device_t* dev = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!dev) {
        serial_puts("e1000: Device not found\n");
        return -1;
    }
    
    // Enable bus mastering and memory space
    uint16_t command = pci_read_config_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    pci_write_config_word(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
    
    // Get MMIO base from BAR0
    uint32_t bar0 = dev->bar[0];
    if (bar0 & 0x1) {
        serial_puts("e1000: BAR0 is I/O (unsupported)\n");
        return -1;
    }
    
    uint32_t mmio_phys = bar0 & 0xFFFFFFF0;
    
    // Map MMIO region (128KB)
    for (uint32_t i = 0; i < 32; i++) {
        map_page(kernel_directory, 
                 mmio_phys + (i * 0x1000), 
                 mmio_phys + (i * 0x1000), 
                 PAGE_PRESENT | PAGE_WRITE);
    }
    mmio_base = (uint8_t*)mmio_phys;
    
    // Reset device
    e1000_write_reg(E1000_REG_CTRL, E1000_CTRL_RST);
    for (volatile int i = 0; i < 100000; i++);
    
    // Disable interrupts during init
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);
    e1000_read_reg(E1000_REG_ICR);
    
    // Initialize descriptor rings
    e1000_init_rx();
    e1000_init_tx();
    
    // Set link up
    uint32_t ctrl = e1000_read_reg(E1000_REG_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    e1000_write_reg(E1000_REG_CTRL, ctrl);
    
    // Clear multicast table
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(E1000_REG_MTA + (i * 4), 0);
    }
    
    // Register network interface
    e1000_iface = net_interface_register("eth0");
    if (!e1000_iface) {
        serial_puts("e1000: Failed to register interface\n");
        return -1;
    }
    
    // Read and set MAC address
    e1000_read_mac_address(&e1000_iface->mac_addr);
    
    uint32_t ral = ((uint32_t)e1000_iface->mac_addr.addr[0]) |
                   ((uint32_t)e1000_iface->mac_addr.addr[1] << 8) |
                   ((uint32_t)e1000_iface->mac_addr.addr[2] << 16) |
                   ((uint32_t)e1000_iface->mac_addr.addr[3] << 24);
    uint32_t rah = ((uint32_t)e1000_iface->mac_addr.addr[4]) |
                   ((uint32_t)e1000_iface->mac_addr.addr[5] << 8) |
                   (1 << 31);  // Address Valid bit
    
    e1000_write_reg(E1000_REG_RAL, ral);
    e1000_write_reg(E1000_REG_RAH, rah);
    
    // Configure interface
    e1000_iface->flags = IFF_BROADCAST;
    e1000_iface->mtu = MTU_SIZE;
    e1000_iface->ip_addr = 0;
    e1000_iface->netmask = 0;
    e1000_iface->gateway = 0;
    e1000_iface->transmit = e1000_iface_transmit;
    e1000_iface->receive = e1000_iface_receive;
    
    net_interface_up(e1000_iface);
    
    serial_puts("e1000: MAC ");
    char mac_str[20];
    mac_to_string(&e1000_iface->mac_addr, mac_str);
    serial_puts(mac_str);
    serial_puts("\n");
    
    // Attempt DHCP
    if (dhcp_discover(e1000_iface) == 0) {
        dhcp_config_t* config = dhcp_get_config();
        dhcp_configure_interface(e1000_iface, config);
    } else {
        // Link-local fallback
        e1000_iface->ip_addr = 0xA9FE0000 |  // 169.254.0.0
                              ((uint32_t)(e1000_iface->mac_addr.addr[4]) << 8) |
                              ((uint32_t)(e1000_iface->mac_addr.addr[5]));
        e1000_iface->netmask = 0xFFFF0000;  // 255.255.0.0
    }
    
    return 0;
}

net_interface_t* e1000_get_interface(void) {
    return e1000_iface;
}
