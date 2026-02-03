/*
 * === AOS HEADER BEGIN ===
 * ./include/dev/e1000.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <net/net.h>

// Intel e1000 Vendor and Device IDs
#define E1000_VENDOR_ID     0x8086
#define E1000_DEVICE_ID     0x100E  // 82540EM

// E1000 Register Offsets
#define E1000_REG_CTRL      0x0000  // Device Control
#define E1000_REG_STATUS    0x0008  // Device Status
#define E1000_REG_EECD      0x0010  // EEPROM Control
#define E1000_REG_EERD      0x0014  // EEPROM Read
#define E1000_REG_CTRL_EXT  0x0018  // Extended Control
#define E1000_REG_MDIC      0x0020  // MDI Control
#define E1000_REG_ICR       0x00C0  // Interrupt Cause Read
#define E1000_REG_IMS       0x00D0  // Interrupt Mask Set
#define E1000_REG_IMC       0x00D8  // Interrupt Mask Clear
#define E1000_REG_RCTL      0x0100  // Receive Control
#define E1000_REG_TCTL      0x0400  // Transmit Control
#define E1000_REG_TIPG      0x0410  // Transmit IPG
#define E1000_REG_RDBAL     0x2800  // RX Descriptor Base Low
#define E1000_REG_RDBAH     0x2804  // RX Descriptor Base High
#define E1000_REG_RDLEN     0x2808  // RX Descriptor Length
#define E1000_REG_RDH       0x2810  // RX Descriptor Head
#define E1000_REG_RDT       0x2818  // RX Descriptor Tail
#define E1000_REG_TDBAL     0x3800  // TX Descriptor Base Low
#define E1000_REG_TDBAH     0x3804  // TX Descriptor Base High
#define E1000_REG_TDLEN     0x3808  // TX Descriptor Length
#define E1000_REG_TDH       0x3810  // TX Descriptor Head
#define E1000_REG_TDT       0x3818  // TX Descriptor Tail
#define E1000_REG_MTA       0x5200  // Multicast Table Array
#define E1000_REG_RAL       0x5400  // Receive Address Low
#define E1000_REG_RAH       0x5404  // Receive Address High

// Control Register Bits
#define E1000_CTRL_RST      (1 << 26)  // Device Reset
#define E1000_CTRL_SLU      (1 << 6)   // Set Link Up
#define E1000_CTRL_ASDE     (1 << 5)   // Auto Speed Detection Enable

// Receive Control Bits
#define E1000_RCTL_EN       (1 << 1)   // Receiver Enable
#define E1000_RCTL_SBP      (1 << 2)   // Store Bad Packets
#define E1000_RCTL_UPE      (1 << 3)   // Unicast Promiscuous
#define E1000_RCTL_MPE      (1 << 4)   // Multicast Promiscuous
#define E1000_RCTL_BAM      (1 << 15)  // Broadcast Accept Mode
#define E1000_RCTL_BSIZE_2K (0 << 16)  // Buffer Size 2048
#define E1000_RCTL_SECRC    (1 << 26)  // Strip Ethernet CRC

// Transmit Control Bits
#define E1000_TCTL_EN       (1 << 1)   // Transmit Enable
#define E1000_TCTL_PSP      (1 << 3)   // Pad Short Packets

// Descriptor Status Bits
#define E1000_TXD_STAT_DD   (1 << 0)   // Descriptor Done
#define E1000_TXD_CMD_EOP   (1 << 0)   // End of Packet
#define E1000_TXD_CMD_IFCS  (1 << 1)   // Insert FCS (CRC)
#define E1000_TXD_CMD_RS    (1 << 3)   // Report Status
#define E1000_RXD_STAT_DD   (1 << 0)   // Descriptor Done
#define E1000_RXD_STAT_EOP  (1 << 1)   // End of Packet

// Ring Buffer Sizes
#define E1000_NUM_RX_DESC   32
#define E1000_NUM_TX_DESC   32
#define E1000_RX_BUFFER_SIZE 2048
#define E1000_TX_BUFFER_SIZE 2048

// Descriptor Structures
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

// E1000 Functions
int e1000_init(void);
int e1000_transmit(const uint8_t* data, uint32_t len);
void e1000_handle_interrupt(void);
net_interface_t* e1000_get_interface(void);

#endif // E1000_H
