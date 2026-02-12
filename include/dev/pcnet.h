/*
 * === AOS HEADER BEGIN ===
 * include/dev/pcnet.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


/*
 * AMD PCnet Network Driver Header
 * Supports PCnet-PCI II (AM79C970A) and PCnet-FAST III (AM79C973)
 */

#ifndef PCNET_H
#define PCNET_H

#include <stdint.h>
#include <net/net.h>

// AMD PCnet Vendor ID
#define PCNET_VENDOR_ID         0x1022  // AMD

// Device IDs for supported chips
#define PCNET_PCI_II_DEVICE_ID  0x2000  // PCnet-PCI II (AM79C970A)
#define PCNET_FAST_III_DEVICE_ID 0x2001 // PCnet-FAST III (AM79C973)

// I/O Port Offsets (16-bit mode)
#define PCNET_IO_APROM0         0x00    // APROM (MAC address bytes 0-3)
#define PCNET_IO_APROM4         0x04    // APROM (MAC address bytes 4-5)
#define PCNET_IO_RDP            0x10    // Register Data Port
#define PCNET_IO_RAP            0x12    // Register Address Port (word)
#define PCNET_IO_RESET          0x14    // Reset register
#define PCNET_IO_BDP            0x16    // Bus Configuration Data Port

// 32-bit I/O Port Offsets
#define PCNET_IO32_RDP          0x10    // Register Data Port (32-bit)
#define PCNET_IO32_RAP          0x14    // Register Address Port (32-bit)
#define PCNET_IO32_RESET        0x18    // Reset register (32-bit)
#define PCNET_IO32_BDP          0x1C    // Bus Configuration Data Port (32-bit)

// CSR (Control and Status Register) Indices
#define PCNET_CSR0              0       // Status and Control
#define PCNET_CSR1              1       // Init Block Address Low
#define PCNET_CSR2              2       // Init Block Address High
#define PCNET_CSR3              3       // Interrupt Masks and Deferral Control
#define PCNET_CSR4              4       // Test and Features Control
#define PCNET_CSR5              5       // Extended Control and Interrupt
#define PCNET_CSR15             15      // Mode
#define PCNET_CSR58             58      // Software Style
#define PCNET_CSR76             76      // Receive Ring Length
#define PCNET_CSR78             78      // Transmit Ring Length
#define PCNET_CSR80             80      // FIFO Threshold
#define PCNET_CSR82             82      // Bus Activity Timer

// BCR (Bus Configuration Register) Indices
#define PCNET_BCR2              2       // Miscellaneous Configuration
#define PCNET_BCR9              9       // Full-Duplex Control
#define PCNET_BCR18             18      // Burst and Bus Control
#define PCNET_BCR19             19      // EEPROM Control and Status
#define PCNET_BCR20             20      // Software Style

// CSR0 Bit Definitions
#define PCNET_CSR0_INIT         (1 << 0)   // Initialize
#define PCNET_CSR0_STRT         (1 << 1)   // Start
#define PCNET_CSR0_STOP         (1 << 2)   // Stop
#define PCNET_CSR0_TDMD         (1 << 3)   // Transmit Demand
#define PCNET_CSR0_TXON         (1 << 4)   // Transmitter On
#define PCNET_CSR0_RXON         (1 << 5)   // Receiver On
#define PCNET_CSR0_IENA         (1 << 6)   // Interrupt Enable
#define PCNET_CSR0_INTR         (1 << 7)   // Interrupt Flag
#define PCNET_CSR0_IDON         (1 << 8)   // Initialization Done
#define PCNET_CSR0_TINT         (1 << 9)   // Transmit Interrupt
#define PCNET_CSR0_RINT         (1 << 10)  // Receive Interrupt
#define PCNET_CSR0_MERR         (1 << 11)  // Memory Error
#define PCNET_CSR0_MISS         (1 << 12)  // Missed Frame
#define PCNET_CSR0_CERR         (1 << 13)  // Collision Error
#define PCNET_CSR0_BABL         (1 << 14)  // Babble Error
#define PCNET_CSR0_ERR          (1 << 15)  // Error

// CSR3 Bit Definitions
#define PCNET_CSR3_BSWP         (1 << 2)   // Byte Swap
#define PCNET_CSR3_EMBA         (1 << 3)   // Enable Modified Back-off Algorithm
#define PCNET_CSR3_DXMT2PD      (1 << 4)   // Disable Transmit Two-Part Deferral
#define PCNET_CSR3_LAPPEN       (1 << 5)   // Look-Ahead Packet Processing Enable
#define PCNET_CSR3_DXSUFLO      (1 << 6)   // Disable Transmit Stop on Underflow
#define PCNET_CSR3_IDONM        (1 << 8)   // Initialization Done Mask
#define PCNET_CSR3_TINTM        (1 << 9)   // Transmit Interrupt Mask
#define PCNET_CSR3_RINTM        (1 << 10)  // Receive Interrupt Mask
#define PCNET_CSR3_MERRM        (1 << 11)  // Memory Error Mask
#define PCNET_CSR3_MISSM        (1 << 12)  // Missed Frame Mask

// CSR4 Bit Definitions
#define PCNET_CSR4_JABM         (1 << 0)   // Jabber Error Mask
#define PCNET_CSR4_JAB          (1 << 1)   // Jabber Error
#define PCNET_CSR4_TXSTRT       (1 << 2)   // Transmit Start Status
#define PCNET_CSR4_TXSTRTM      (1 << 3)   // Transmit Start Mask
#define PCNET_CSR4_RCVCCO       (1 << 4)   // Receive Collision Counter Overflow
#define PCNET_CSR4_RCVCCOM      (1 << 5)   // Receive Collision Counter Overflow Mask
#define PCNET_CSR4_MFCO         (1 << 6)   // Missed Frame Counter Overflow
#define PCNET_CSR4_MFCOM        (1 << 7)   // Missed Frame Counter Overflow Mask
#define PCNET_CSR4_ASTRP_RCV    (1 << 10)  // Auto Pad Strip on Receive
#define PCNET_CSR4_APAD_XMT     (1 << 11)  // Auto Pad Transmit
#define PCNET_CSR4_DPOLL        (1 << 12)  // Disable Polling
#define PCNET_CSR4_TIMER        (1 << 13)  // Timer Enable
#define PCNET_CSR4_DMAPLUS      (1 << 14)  // DMA Burst Transfer Mode
#define PCNET_CSR4_EN124        (1 << 15)  // Enable CSR124

// CSR15 Mode Bits
#define PCNET_CSR15_DRX         (1 << 0)   // Disable Receiver
#define PCNET_CSR15_DTX         (1 << 1)   // Disable Transmitter
#define PCNET_CSR15_LOOP        (1 << 2)   // Loopback Enable
#define PCNET_CSR15_DXMTFCS     (1 << 3)   // Disable Transmit FCS
#define PCNET_CSR15_FCOLL       (1 << 4)   // Force Collision
#define PCNET_CSR15_DRTY        (1 << 5)   // Disable Retry
#define PCNET_CSR15_INTL        (1 << 6)   // Internal Loopback
#define PCNET_CSR15_PORTSEL0    (1 << 7)   // Port Select Bit 0
#define PCNET_CSR15_PORTSEL1    (1 << 8)   // Port Select Bit 1
#define PCNET_CSR15_LRT         (1 << 9)   // Low Receive Threshold
#define PCNET_CSR15_TSEL        (1 << 9)   // Transmit Mode Select
#define PCNET_CSR15_MENDECL     (1 << 10)  // MENDEC Loopback Mode
#define PCNET_CSR15_DAPC        (1 << 11)  // Disable Auto Polarity Correction
#define PCNET_CSR15_DLNKTST     (1 << 12)  // Disable Link Status
#define PCNET_CSR15_DRCVPA      (1 << 13)  // Disable Receive Physical Address
#define PCNET_CSR15_DRCVBC      (1 << 14)  // Disable Receive Broadcast
#define PCNET_CSR15_PROM        (1 << 15)  // Promiscuous Mode

// BCR2 Bit Definitions
#define PCNET_BCR2_ASEL         (1 << 1)   // Auto-Select Media
#define PCNET_BCR2_AWAKE        (1 << 2)   // Awake
#define PCNET_BCR2_EADISEL      (1 << 3)   // EADI Select
#define PCNET_BCR2_XMAUSEL      (1 << 4)   // External MAU Select

// BCR18 Bit Definitions
#define PCNET_BCR18_BWRITE      (1 << 5)   // Burst Write Enable
#define PCNET_BCR18_BREADE      (1 << 6)   // Burst Read Enable
#define PCNET_BCR18_DWIO        (1 << 7)   // 32-bit Word I/O

// BCR20 Software Style Definitions
#define PCNET_SWSTYLE_LANCE     0          // 16-bit software structure
#define PCNET_SWSTYLE_ILACC     1          // 32-bit software structure
#define PCNET_SWSTYLE_PCNETPCI  2          // PCnet-PCI 32-bit
#define PCNET_SWSTYLE_PCNETPCI_BURST 3     // PCnet-PCI 32-bit burst

// Ring Buffer Sizes (must be power of 2)
#define PCNET_NUM_RX_DESC       32
#define PCNET_NUM_TX_DESC       32
#define PCNET_RX_BUFFER_SIZE    1544       // Max Ethernet frame + extra
#define PCNET_TX_BUFFER_SIZE    1544

// Descriptor Ownership
#define PCNET_DESC_OWN          (1 << 31)  // Descriptor owned by chip
#define PCNET_DESC_ERR          (1 << 30)  // Error summary
#define PCNET_DESC_STP          (1 << 25)  // Start of Packet
#define PCNET_DESC_ENP          (1 << 24)  // End of Packet
#define PCNET_DESC_BPE          (1 << 23)  // Bus Parity Error
#define PCNET_DESC_PAM          (1 << 22)  // Physical Address Match
#define PCNET_DESC_LAFM         (1 << 21)  // Logical Address Filter Match
#define PCNET_DESC_BAM          (1 << 20)  // Broadcast Address Match

// TX Descriptor Flags
#define PCNET_TXDESC_ADD_FCS    (1 << 29)  // Add FCS
#define PCNET_TXDESC_MORE       (1 << 28)  // More than one retry
#define PCNET_TXDESC_ONE        (1 << 27)  // One retry
#define PCNET_TXDESC_DEF        (1 << 26)  // Deferred

// Initialization Block Mode
#define PCNET_MODE_PROM         (1 << 15)  // Promiscuous Mode
#define PCNET_MODE_DRCVBC       (1 << 14)  // Disable Receive Broadcast
#define PCNET_MODE_DRCVPA       (1 << 13)  // Disable Receive Physical Address

// 32-bit Receive Descriptor (PCnet-PCI SWSTYLE 2)
// Format: RBADR | STATUS:BCNT | MCNT | RESERVED
// STATUS:BCNT = (FLAGS[15:0] << 16) | (0xF << 12) | BCNT[11:0]
// OWN bit is bit 31 of status_bcnt field
typedef struct {
    uint32_t rbadr;       // Offset 0: Buffer Address
    uint32_t status_bcnt; // Offset 4: [31:16]=Status flags (OWN at 31), [15:12]=0xF, [11:0]=BCNT
    uint32_t mcnt_flags;  // Offset 8: [11:0]=Message count, upper bits=misc flags
    uint32_t reserved;    // Offset 12: Reserved/User
} __attribute__((packed)) pcnet_rx_desc_t;

// 32-bit Transmit Descriptor (PCnet-PCI SWSTYLE 2)
typedef struct {
    uint32_t tbadr;       // Offset 0: Buffer Address
    uint32_t status_bcnt; // Offset 4: [31:16]=Status flags (OWN at 31), [15:12]=0xF, [11:0]=BCNT
    uint32_t misc;        // Offset 8: TRC and misc
    uint32_t reserved;    // Offset 12: Reserved/User
} __attribute__((packed)) pcnet_tx_desc_t;

// 32-bit Initialization Block (Software Style 2)
typedef struct {
    uint16_t mode;       // Mode (CSR15 value)
    uint8_t  rlen;       // Receive Descriptor Ring Length (encoded)
    uint8_t  tlen;       // Transmit Descriptor Ring Length (encoded)
    uint8_t  padr[6];    // Physical (MAC) Address
    uint16_t reserved;   // Reserved
    uint8_t  ladrf[8];   // Logical Address Filter
    uint32_t rdra;       // Receive Descriptor Ring Address
    uint32_t tdra;       // Transmit Descriptor Ring Address
} __attribute__((packed)) pcnet_init_block_t;

// PCnet Device Types
typedef enum {
    PCNET_TYPE_UNKNOWN = 0,
    PCNET_TYPE_PCI_II,       // AM79C970A
    PCNET_TYPE_FAST_III,     // AM79C973
} pcnet_type_t;

// PCnet Driver Functions
int pcnet_init(void);
int pcnet_transmit(const uint8_t* data, uint32_t len);
void pcnet_handle_interrupt(void);
net_interface_t* pcnet_get_interface(void);
pcnet_type_t pcnet_get_type(void);
const char* pcnet_get_type_string(void);

#endif // PCNET_H
