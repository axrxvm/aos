/*
 * === AOS HEADER BEGIN ===
 * src/dev/pcnet.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/* 
 * AMD PCnet Network Driver
 * Supports PCnet-PCI II (AM79C970A) and PCnet-FAST III (AM79C973)
 * 
 * The PCnet family uses a ring buffer DMA architecture similar to e1000.
 * Features auto-negotiation, full-duplex support, and hardware address filtering.
 */

#include <dev/pcnet.h>
#include <dev/pci.h>
#include <net/net.h>
#include <net/ethernet.h>
#include <net/arp.h>
#include <net/dhcp.h>
#include <arch/paging.h>
#include <io.h>
#include <serial.h>
#include <string.h>
#include <vmm.h>
#include <stdlib.h>

// PCnet Device State
static uint16_t io_base = 0;
static pcnet_type_t device_type = PCNET_TYPE_UNKNOWN;
static net_interface_t* pcnet_iface = NULL;

// Initialization Block (16-byte aligned for DMA)
static pcnet_init_block_t* init_block = NULL;

// Descriptor Rings (16-byte aligned for DMA)
static pcnet_rx_desc_t* rx_descs = NULL;
static pcnet_tx_desc_t* tx_descs = NULL;

// Buffer Pools
static uint8_t* rx_buffers[PCNET_NUM_RX_DESC];
static uint8_t* tx_buffers[PCNET_NUM_TX_DESC];

// Ring Indices
static volatile uint32_t rx_head = 0;
static volatile uint32_t tx_tail = 0;

// Statistics
static uint32_t tx_packets = 0;
static uint32_t rx_packets = 0;

// Keep boot networking responsive: avoid long DHCP blocking during startup.
#define PCNET_BOOT_DHCP_TIMEOUT_TICKS 100
// I/O Access Functions

static inline uint16_t pcnet_read_csr16(uint8_t csr) {
    outw(io_base + PCNET_IO_RAP, csr);
    return inw(io_base + PCNET_IO_RDP);
}

static inline void pcnet_write_csr16(uint8_t csr, uint16_t value) {
    outw(io_base + PCNET_IO_RAP, csr);
    outw(io_base + PCNET_IO_RDP, value);
}

static inline uint32_t pcnet_read_csr32(uint8_t csr) {
    outl(io_base + PCNET_IO32_RAP, csr);
    return inl(io_base + PCNET_IO32_RDP);
}

static inline void pcnet_write_csr32(uint8_t csr, uint32_t value) {
    outl(io_base + PCNET_IO32_RAP, csr);
    outl(io_base + PCNET_IO32_RDP, value);
}

static inline uint16_t pcnet_read_bcr16(uint8_t bcr) {
    outw(io_base + PCNET_IO_RAP, bcr);
    return inw(io_base + PCNET_IO_BDP);
}

static inline void pcnet_write_bcr16(uint8_t bcr, uint16_t value) {
    outw(io_base + PCNET_IO_RAP, bcr);
    outw(io_base + PCNET_IO_BDP, value);
}

static inline uint32_t pcnet_read_bcr32(uint8_t bcr) {
    outl(io_base + PCNET_IO32_RAP, bcr);
    return inl(io_base + PCNET_IO32_BDP);
}

static inline void pcnet_write_bcr32(uint8_t bcr, uint32_t value) {
    outl(io_base + PCNET_IO32_RAP, bcr);
    outl(io_base + PCNET_IO32_BDP, value);
}

// Device Reset

static void pcnet_reset(void) {
    // Reading from the reset register triggers a reset (16-bit mode)
    inw(io_base + PCNET_IO_RESET);
    // Small delay for reset to complete
    for (volatile int i = 0; i < 100000; i++);
    
    // Write 0 to CSR0 to ensure STOP is cleared
    pcnet_write_csr16(PCNET_CSR0, 0);
}

// MAC Address Functions

static void pcnet_read_mac_address(mac_addr_t* mac) {
    // MAC address is stored in the APROM (Address PROM)
    // MUST use byte-wide reads at offsets 0x00-0x05
    // The APROM is accessible after a reset in 16-bit I/O mode
    mac->addr[0] = inb(io_base + 0x00);
    mac->addr[1] = inb(io_base + 0x01);
    mac->addr[2] = inb(io_base + 0x02);
    mac->addr[3] = inb(io_base + 0x03);
    mac->addr[4] = inb(io_base + 0x04);
    mac->addr[5] = inb(io_base + 0x05);
}

// Descriptor Ring Initialization

// Encode ring length (returns the encoded value for init block)
// Length must be a power of 2 between 1 and 512
static uint8_t pcnet_encode_ring_length(int length) {
    int log2 = 0;
    while ((1 << log2) < length && log2 < 9) {
        log2++;
    }
    return (uint8_t)(log2 << 4);  // Shifted for init block format
}

static void pcnet_init_rx_ring(void) {
    // Allocate descriptor ring (16-byte aligned)
    uint32_t desc_size = sizeof(pcnet_rx_desc_t) * PCNET_NUM_RX_DESC;
    uint8_t* raw_desc = (uint8_t*)kmalloc(desc_size + 16);
    rx_descs = (pcnet_rx_desc_t*)(((uintptr_t)raw_desc + 15) & ~15);
    memset(rx_descs, 0, desc_size);
    
    // Allocate and setup buffers
    for (int i = 0; i < PCNET_NUM_RX_DESC; i++) {
        uint8_t* raw_buf = (uint8_t*)kmalloc(PCNET_RX_BUFFER_SIZE + 16);
        rx_buffers[i] = (uint8_t*)(((uintptr_t)raw_buf + 15) & ~15);
        
        rx_descs[i].rbadr = (uint32_t)(uintptr_t)rx_buffers[i];
        // status_bcnt format: [31]=OWN, [30:16]=status, [15:12]=0xF (ones), [11:0]=BCNT (two's complement)
        uint16_t bcnt = (uint16_t)((-PCNET_RX_BUFFER_SIZE) & 0x0FFF) | 0xF000;  // BCNT with ONES bits
        rx_descs[i].status_bcnt = PCNET_DESC_OWN | bcnt;  // OWN=1, give to chip
        rx_descs[i].mcnt_flags = 0;
        rx_descs[i].reserved = 0;
    }
    
    rx_head = 0;
}

static void pcnet_init_tx_ring(void) {
    // Allocate descriptor ring (16-byte aligned)
    uint32_t desc_size = sizeof(pcnet_tx_desc_t) * PCNET_NUM_TX_DESC;
    uint8_t* raw_desc = (uint8_t*)kmalloc(desc_size + 16);
    tx_descs = (pcnet_tx_desc_t*)(((uintptr_t)raw_desc + 15) & ~15);
    memset(tx_descs, 0, desc_size);
    
    // Allocate and setup buffers
    for (int i = 0; i < PCNET_NUM_TX_DESC; i++) {
        uint8_t* raw_buf = (uint8_t*)kmalloc(PCNET_TX_BUFFER_SIZE + 16);
        tx_buffers[i] = (uint8_t*)(((uintptr_t)raw_buf + 15) & ~15);
        
        tx_descs[i].tbadr = (uint32_t)(uintptr_t)tx_buffers[i];
        tx_descs[i].status_bcnt = 0xF000;  // ONES bits set, OWN=0 (we own it)
        tx_descs[i].misc = 0;
        tx_descs[i].reserved = 0;
    }
    
    tx_tail = 0;
}

static void pcnet_init_block_setup(void) {
    // Allocate init block (16-byte aligned)
    uint8_t* raw_block = (uint8_t*)kmalloc(sizeof(pcnet_init_block_t) + 16);
    init_block = (pcnet_init_block_t*)(((uintptr_t)raw_block + 15) & ~15);
    memset(init_block, 0, sizeof(pcnet_init_block_t));
    
    // Set mode (promiscuous for now to receive all packets including DHCP)
    init_block->mode = PCNET_MODE_PROM;
    
    // Set ring lengths (encoded as log2 shifted left by 4)
    init_block->rlen = pcnet_encode_ring_length(PCNET_NUM_RX_DESC);
    init_block->tlen = pcnet_encode_ring_length(PCNET_NUM_TX_DESC);
    
    // Copy MAC address
    for (int i = 0; i < 6; i++) {
        init_block->padr[i] = pcnet_iface->mac_addr.addr[i];
    }
    
    // Set logical address filter to accept all multicast (all 1s)
    for (int i = 0; i < 8; i++) {
        init_block->ladrf[i] = 0xFF;
    }
    
    // Set descriptor ring addresses
    init_block->rdra = (uint32_t)(uintptr_t)rx_descs;
    init_block->tdra = (uint32_t)(uintptr_t)tx_descs;
}

// Packet Transmission

int pcnet_transmit(const uint8_t* data, uint32_t len) {
    if (!io_base || !data || len == 0 || len > PCNET_TX_BUFFER_SIZE) {
        return -1;
    }
    
    // Get current descriptor
    uint32_t desc_idx = tx_tail;
    pcnet_tx_desc_t* desc = &tx_descs[desc_idx];
    
    // Wait for descriptor to be available (not owned by chip)
    int timeout = 100000;
    while ((desc->status_bcnt & PCNET_DESC_OWN) && timeout-- > 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }
    
    if (timeout <= 0) {
        serial_puts("pcnet: TX timeout waiting for descriptor\n");
        return -1;
    }
    
    // Copy data to buffer
    memcpy(tx_buffers[desc_idx], data, len);
    
    // Setup descriptor: status_bcnt format
    // [31]=OWN, [30:16]=flags (STP, ENP, ADD_FCS), [15:12]=0xF, [11:0]=BCNT
    uint16_t bcnt = (uint16_t)((-len) & 0x0FFF) | 0xF000;  // BCNT with ONES bits
    // Set OWN, STP, ENP flags in upper 16 bits
    desc->status_bcnt = PCNET_DESC_OWN | PCNET_DESC_STP | PCNET_DESC_ENP | PCNET_TXDESC_ADD_FCS | bcnt;
    desc->misc = 0;
    
    // Memory barrier
    __asm__ __volatile__("mfence" ::: "memory");
    
    // Trigger transmit by setting TDMD in CSR0
    uint16_t csr0 = pcnet_read_csr16(PCNET_CSR0);
    pcnet_write_csr16(PCNET_CSR0, csr0 | PCNET_CSR0_TDMD);
    
    // Wait for transmission to complete
    timeout = 100000;
    while ((desc->status_bcnt & PCNET_DESC_OWN) && timeout-- > 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }
    
    // Advance tail
    tx_tail = (tx_tail + 1) % PCNET_NUM_TX_DESC;
    tx_packets++;
    
    // Check for errors
    if (desc->status_bcnt & PCNET_DESC_ERR) {
        serial_puts("pcnet: TX error, status=0x");
        char hex[16];
        itoa(desc->status_bcnt, hex, 16);
        serial_puts(hex);
        serial_puts("\n");
        return -1;
    }
    
    serial_puts("pcnet: TX OK len=");
    char len_str[16];
    itoa(len, len_str, 10);
    serial_puts(len_str);
    serial_puts("\n");
    
    return 0;
}

// Packet Reception

static void pcnet_receive(void) {
    while (!(rx_descs[rx_head].status_bcnt & PCNET_DESC_OWN)) {
        pcnet_rx_desc_t* desc = &rx_descs[rx_head];
        
        // Memory barrier
        __asm__ __volatile__("lfence" ::: "memory");
        
        // Check for errors (ERR bit is bit 30)
        if (desc->status_bcnt & PCNET_DESC_ERR) {
            serial_puts("pcnet: RX error\n");
        } else {
            // Get message length from mcnt_flags (lower 12 bits)
            uint16_t length = desc->mcnt_flags & 0x0FFF;
            
            if (length > 0 && length <= PCNET_RX_BUFFER_SIZE && pcnet_iface) {
                rx_packets++;
                
                serial_puts("pcnet: Rx packet len=");
                char len_str[16];
                itoa(length, len_str, 10);
                serial_puts(len_str);
                serial_puts("\n");
                
                // Process packet through Ethernet layer
                net_packet_t packet;
                packet.data = rx_buffers[rx_head];
                packet.len = length;
                packet.capacity = PCNET_RX_BUFFER_SIZE;
                
                eth_receive(pcnet_iface, &packet);
            }
        }
        
        // Reset descriptor for reuse
        uint16_t bcnt = (uint16_t)((-PCNET_RX_BUFFER_SIZE) & 0x0FFF) | 0xF000;
        desc->status_bcnt = PCNET_DESC_OWN | bcnt;  // Give back to chip
        desc->mcnt_flags = 0;
        
        // Advance head
        rx_head = (rx_head + 1) % PCNET_NUM_RX_DESC;
    }
}

void pcnet_handle_interrupt(void) {
    if (!io_base) return;
    
    static uint32_t poll_count = 0;
    poll_count++;
    
    // Debug every 50000 polls to reduce spam
    if (poll_count % 50000 == 0) {
        serial_puts("pcnet: poll #");
        char count_str[16];
        itoa(poll_count / 1000, count_str, 10);
        serial_puts(count_str);
        serial_puts("k, desc[0]=0x");
        itoa(rx_descs[0].status_bcnt, count_str, 16);
        serial_puts(count_str);
        serial_puts("\n");
    }
    
    // Read and clear interrupt status
    uint16_t csr0 = pcnet_read_csr16(PCNET_CSR0);
    
    // Debug: Check if anything interesting is happening
    if (csr0 & (PCNET_CSR0_RINT | PCNET_CSR0_TINT | PCNET_CSR0_ERR)) {
        serial_puts("pcnet: CSR0=0x");
        char hex[8];
        itoa(csr0, hex, 16);
        serial_puts(hex);
        serial_puts("\n");
    }
    
    // Clear interrupt flags by writing 1s to them
    pcnet_write_csr16(PCNET_CSR0, csr0 & 
        (PCNET_CSR0_TINT | PCNET_CSR0_RINT | PCNET_CSR0_MERR | 
         PCNET_CSR0_MISS | PCNET_CSR0_CERR | PCNET_CSR0_BABL));
    
    // Handle receive interrupt
    if (csr0 & PCNET_CSR0_RINT) {
        pcnet_receive();
    }
    
    // Handle errors
    if (csr0 & PCNET_CSR0_ERR) {
        if (csr0 & PCNET_CSR0_BABL) serial_puts("pcnet: Babble error\n");
        if (csr0 & PCNET_CSR0_CERR) serial_puts("pcnet: Collision error\n");
        if (csr0 & PCNET_CSR0_MISS) serial_puts("pcnet: Missed frame\n");
        if (csr0 & PCNET_CSR0_MERR) serial_puts("pcnet: Memory error\n");
    }
}

// Network Interface Functions

static int pcnet_iface_transmit(net_interface_t* iface, net_packet_t* packet) {
    (void)iface;
    
    if (!packet || !packet->data || packet->len == 0) {
        return -1;
    }
    
    return pcnet_transmit(packet->data, packet->len);
}

static int pcnet_iface_receive(net_interface_t* iface, net_packet_t* packet) {
    (void)iface;
    (void)packet;
    return 0;  // Handled by polling/interrupts
}

// Driver Initialization

int pcnet_init(void) {
    serial_puts("pcnet: Initializing...\n");
    
    // Try to find PCnet-FAST III first
    pci_device_t* dev = pci_find_device(PCNET_VENDOR_ID, PCNET_FAST_III_DEVICE_ID);
    if (dev) {
        device_type = PCNET_TYPE_FAST_III;
        serial_puts("pcnet: Found PCnet-FAST III (AM79C973)\n");
    } else {
        // Try PCnet-PCI II
        dev = pci_find_device(PCNET_VENDOR_ID, PCNET_PCI_II_DEVICE_ID);
        if (dev) {
            device_type = PCNET_TYPE_PCI_II;
            serial_puts("pcnet: Found PCnet-PCI II (AM79C970A)\n");
        }
    }
    
    if (!dev) {
        serial_puts("pcnet: No supported device found\n");
        return -1;
    }
    
    // Enable bus mastering and I/O space access
    uint16_t command = pci_read_config_word(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_write_config_word(dev->bus, dev->device, dev->function, PCI_COMMAND, command);
    
    // Get I/O base from BAR0
    uint32_t bar0 = dev->bar[0];
    if (!(bar0 & 0x1)) {
        serial_puts("pcnet: BAR0 is MMIO (using I/O mode expected)\n");
        // PCnet supports both I/O and MMIO, but we use I/O for simplicity
        return -1;
    }
    
    io_base = (uint16_t)(bar0 & 0xFFFC);
    
    serial_puts("pcnet: I/O base at 0x");
    char buf[16];
    itoa(io_base, buf, 16);
    serial_puts(buf);
    serial_puts("\n");
    
    // Reset the device
    pcnet_reset();
    
    // Register network interface early (need it for MAC address storage)
    pcnet_iface = net_interface_register("eth1");
    if (!pcnet_iface) {
        serial_puts("pcnet: Failed to register interface\n");
        return -1;
    }
    
    // Read MAC address
    pcnet_read_mac_address(&pcnet_iface->mac_addr);
    
    // Set 32-bit software style (PCnet-PCI style)
    // This uses 32-bit descriptors
    pcnet_write_bcr16(PCNET_BCR20, PCNET_SWSTYLE_PCNETPCI);
    
    // Enable auto-select for media type
    uint16_t bcr2 = pcnet_read_bcr16(PCNET_BCR2);
    bcr2 |= PCNET_BCR2_ASEL;
    pcnet_write_bcr16(PCNET_BCR2, bcr2);
    
    // Enable burst mode for better performance (if supported)
    if (device_type == PCNET_TYPE_FAST_III) {
        uint16_t bcr18 = pcnet_read_bcr16(PCNET_BCR18);
        bcr18 |= PCNET_BCR18_BREADE | PCNET_BCR18_BWRITE;
        pcnet_write_bcr16(PCNET_BCR18, bcr18);
    }
    
    // Initialize descriptor rings
    pcnet_init_rx_ring();
    pcnet_init_tx_ring();
    
    // Setup initialization block
    pcnet_init_block_setup();
    
    // Debug: Show init block details
    serial_puts("pcnet: Init block at 0x");
    char addr_hex[16];
    itoa((uint32_t)(uintptr_t)init_block, addr_hex, 16);
    serial_puts(addr_hex);
    serial_puts(", mode=0x");
    itoa(init_block->mode, addr_hex, 16);
    serial_puts(addr_hex);
    serial_puts(", rlen=0x");
    itoa(init_block->rlen, addr_hex, 16);
    serial_puts(addr_hex);
    serial_puts(", tlen=0x");
    itoa(init_block->tlen, addr_hex, 16);
    serial_puts(addr_hex);
    serial_puts("\n");
    serial_puts("pcnet: RX ring at 0x");
    itoa(init_block->rdra, addr_hex, 16);
    serial_puts(addr_hex);
    serial_puts(", TX ring at 0x");
    itoa(init_block->tdra, addr_hex, 16);
    serial_puts(addr_hex);
    serial_puts("\n");
    
    // Write init block address to CSR1 (low) and CSR2 (high)
    uint32_t init_addr = (uint32_t)(uintptr_t)init_block;
    pcnet_write_csr16(PCNET_CSR1, (uint16_t)(init_addr & 0xFFFF));
    pcnet_write_csr16(PCNET_CSR2, (uint16_t)((init_addr >> 16) & 0xFFFF));
    
    // Start initialization
    pcnet_write_csr16(PCNET_CSR0, PCNET_CSR0_INIT);
    
    // Wait for initialization to complete
    int timeout = 100000;
    while (!(pcnet_read_csr16(PCNET_CSR0) & PCNET_CSR0_IDON) && timeout-- > 0) {
        __asm__ __volatile__("pause" ::: "memory");
    }
    
    if (timeout <= 0) {
        serial_puts("pcnet: Initialization timeout\n");
        return -1;
    }
    
    // PCnet initialization sequence per AMD datasheet:
    // 1. After IDON is set, chip is still in INIT state
    // 2. Write STOP to exit initialization mode
    // 3. Then write STRT to begin operation
    
    // Step 1: Stop initialization mode (clears INIT)
    pcnet_write_csr16(PCNET_CSR0, PCNET_CSR0_STOP);
    
    // Small delay for chip to transition
    for (volatile int i = 0; i < 1000; i++);
    
    // Step 2: Start the chip (begins TX/RX operation)
    pcnet_write_csr16(PCNET_CSR0, PCNET_CSR0_STRT);
    
    // Delay for chip to fully enter run mode
    for (volatile int i = 0; i < 10000; i++);
    
    // Verify running
    uint16_t csr0 = pcnet_read_csr16(PCNET_CSR0);
    if (!(csr0 & (PCNET_CSR0_TXON | PCNET_CSR0_RXON))) {
        serial_puts("pcnet: Failed to start TX/RX\n");
        return -1;
    }
    
    serial_puts("pcnet: CSR0 after start: 0x");
    char csr_hex[8];
    itoa(csr0, csr_hex, 16);
    serial_puts(csr_hex);
    serial_puts("\n");
    
    // Configure interface
    pcnet_iface->flags = IFF_BROADCAST;
    pcnet_iface->mtu = MTU_SIZE;
    pcnet_iface->ip_addr = 0;
    pcnet_iface->netmask = 0;
    pcnet_iface->gateway = 0;
    pcnet_iface->transmit = pcnet_iface_transmit;
    pcnet_iface->receive = pcnet_iface_receive;
    
    net_interface_up(pcnet_iface);
    
    serial_puts("pcnet: MAC ");
    char mac_str[20];
    mac_to_string(&pcnet_iface->mac_addr, mac_str);
    serial_puts(mac_str);
    serial_puts("\n");
    
    // Attempt DHCP
    serial_puts("pcnet: Starting DHCP...\n");
    if (dhcp_discover_timed(pcnet_iface,
                            PCNET_BOOT_DHCP_TIMEOUT_TICKS,
                            PCNET_BOOT_DHCP_TIMEOUT_TICKS) == 0) {
        dhcp_config_t* config = dhcp_get_config();
        dhcp_configure_interface(pcnet_iface, config);
        serial_puts("pcnet: DHCP OK - ");
        serial_puts(ip_to_string(pcnet_iface->ip_addr));
        serial_puts("\n");
    } else {
        // Link-local fallback
        serial_puts("pcnet: DHCP failed, using link-local\n");
        pcnet_iface->ip_addr = 0xA9FE0000 |  // 169.254.0.0
                              ((uint32_t)(pcnet_iface->mac_addr.addr[4]) << 8) |
                              ((uint32_t)(pcnet_iface->mac_addr.addr[5]));
        pcnet_iface->netmask = 0xFFFF0000;  // 255.255.0.0
        
        serial_puts("pcnet: IP ");
        serial_puts(ip_to_string(pcnet_iface->ip_addr));
        serial_puts("\n");
    }
    
    serial_puts("pcnet: Initialization complete\n");
    return 0;
}

net_interface_t* pcnet_get_interface(void) {
    return pcnet_iface;
}

pcnet_type_t pcnet_get_type(void) {
    return device_type;
}

const char* pcnet_get_type_string(void) {
    switch (device_type) {
        case PCNET_TYPE_PCI_II:
            return "PCnet-PCI II (AM79C970A)";
        case PCNET_TYPE_FAST_III:
            return "PCnet-FAST III (AM79C973)";
        default:
            return "Unknown";
    }
}
