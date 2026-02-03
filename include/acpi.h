/*
 * === AOS HEADER BEGIN ===
 * ./include/acpi.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/* 
 * ACPI (Advanced Configuration and Power Interface) Support
 * Provides system power management and shutdown capabilities
 */

#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>

// ACPI RSDP (Root System Description Pointer) signatures
#define RSDP_SIGNATURE "RSD PTR "
#define RSDP_SIGNATURE_LEN 8

// ACPI Table signatures
#define RSDT_SIGNATURE "RSDT"
#define FACP_SIGNATURE "FACP"  // Fixed ACPI Description Table (FADT)
#define DSDT_SIGNATURE "DSDT"

// ACPI Enable/Disable values
#define ACPI_ENABLE  0x01
#define ACPI_DISABLE 0x00

// Sleep states (SLP_TYPx values)
#define SLP_TYP_S5   0x05  // Soft-off state

// PM1 Control Register bits
#define PM1_CNT_SLP_EN    (1 << 13)  // Sleep Enable bit
#define PM1_CNT_SLP_TYP_SHIFT 10     // Sleep Type shift

// Common QEMU/Bochs ACPI ports (fallback)
#define QEMU_ACPI_SHUTDOWN_PORT  0x604
#define QEMU_ACPI_SHUTDOWN_VALUE 0x2000
#define BOCHS_ACPI_PORT          0xB004
#define BOCHS_ACPI_SHUTDOWN_VAL  0x2000
#define VIRTUALBOX_ACPI_PORT     0x4004
#define VIRTUALBOX_SHUTDOWN_VAL  0x3400

// RSDP Structure (ACPI 1.0)
typedef struct {
    char signature[8];       // "RSD PTR "
    uint8_t checksum;        // Checksum of first 20 bytes
    char oem_id[6];          // OEM identifier
    uint8_t revision;        // ACPI revision (0 for 1.0, 2 for 2.0+)
    uint32_t rsdt_address;   // Physical address of RSDT
} __attribute__((packed)) rsdp_t;

// RSDP Structure (ACPI 2.0+)
typedef struct {
    rsdp_t first_part;       // ACPI 1.0 compatible part
    uint32_t length;         // Length of the table
    uint64_t xsdt_address;   // Physical address of XSDT (64-bit)
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) rsdp2_t;

// Standard ACPI Table Header
typedef struct {
    char signature[4];       // Table signature
    uint32_t length;         // Total table length
    uint8_t revision;        // Table revision
    uint8_t checksum;        // Checksum
    char oem_id[6];          // OEM identifier
    char oem_table_id[8];    // OEM table identifier
    uint32_t oem_revision;   // OEM revision
    uint32_t creator_id;     // Creator ID
    uint32_t creator_revision; // Creator revision
} __attribute__((packed)) acpi_header_t;

// RSDT (Root System Description Table)
typedef struct {
    acpi_header_t header;
    uint32_t entry[];        // Array of physical addresses of other tables
} __attribute__((packed)) rsdt_t;

// Generic Address Structure (ACPI 2.0+)
typedef struct {
    uint8_t address_space;   // Address space ID
    uint8_t bit_width;       // Register bit width
    uint8_t bit_offset;      // Register bit offset
    uint8_t access_size;     // Access size
    uint64_t address;        // Register address
} __attribute__((packed)) generic_address_t;

// FADT (Fixed ACPI Description Table) - ACPI 1.0 compatible
typedef struct {
    acpi_header_t header;
    uint32_t firmware_ctrl;      // Physical address of FACS
    uint32_t dsdt;               // Physical address of DSDT
    uint8_t reserved;            // Reserved in ACPI 2.0+
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;      // System Control Interrupt
    uint32_t smi_command_port;   // SMI Command Port
    uint8_t acpi_enable;         // Value to write to enable ACPI
    uint8_t acpi_disable;        // Value to write to disable ACPI
    uint8_t s4bios_req;          // Value to write for S4BIOS state
    uint8_t pstate_control;      // Processor performance state control
    uint32_t pm1a_event_block;   // PM1a Event Register Block
    uint32_t pm1b_event_block;   // PM1b Event Register Block
    uint32_t pm1a_control_block; // PM1a Control Register Block
    uint32_t pm1b_control_block; // PM1b Control Register Block
    uint32_t pm2_control_block;  // PM2 Control Register Block
    uint32_t pm_timer_block;     // PM Timer Block
    uint32_t gpe0_block;         // General Purpose Event 0 Block
    uint32_t gpe1_block;         // General Purpose Event 1 Block
    uint8_t pm1_event_length;    // Bytes decoded by PM1a/b Event Block
    uint8_t pm1_control_length;  // Bytes decoded by PM1a/b Control Block
    uint8_t pm2_control_length;  // Bytes decoded by PM2 Control Block
    uint8_t pm_timer_length;     // Bytes decoded by PM Timer Block
    uint8_t gpe0_length;         // Bytes decoded by GPE0 Block
    uint8_t gpe1_length;         // Bytes decoded by GPE1 Block
    uint8_t gpe1_base;           // Offset in GPE numbering
    uint8_t c_state_control;     // C-state control support
    uint16_t worst_c2_latency;   // Worst case C2 latency
    uint16_t worst_c3_latency;   // Worst case C3 latency
    uint16_t flush_size;         // Cache flush size
    uint16_t flush_stride;       // Cache flush stride
    uint8_t duty_offset;         // P_CNT duty cycle offset
    uint8_t duty_width;          // P_CNT duty cycle width
    uint8_t day_alarm;           // RTC day of month alarm
    uint8_t month_alarm;         // RTC month of year alarm
    uint8_t century;             // RTC century
    uint16_t boot_arch_flags;    // Boot architecture flags (ACPI 2.0+)
    uint8_t reserved2;           // Reserved
    uint32_t flags;              // Fixed feature flags
    // ACPI 2.0+ fields follow but are not needed for basic shutdown
} __attribute__((packed)) fadt_t;

// ACPI subsystem state
typedef struct {
    bool initialized;            // ACPI initialized successfully
    bool enabled;                // ACPI mode enabled
    uint8_t revision;            // ACPI revision
    uint32_t pm1a_control;       // PM1a Control Register port
    uint32_t pm1b_control;       // PM1b Control Register port
    uint16_t slp_typa;           // Sleep Type for PM1a
    uint16_t slp_typb;           // Sleep Type for PM1b
    uint32_t smi_cmd;            // SMI Command port
    uint8_t acpi_enable_val;     // Value to enable ACPI
    const fadt_t* fadt;          // Pointer to FADT
    const rsdt_t* rsdt;          // Pointer to RSDT
} acpi_state_t;

// Initialize ACPI subsystem
// Returns: 0 on success, negative on error
int acpi_init(void);

// Check if ACPI is available and initialized
bool acpi_available(void);

// Enable ACPI mode (switch from legacy mode)
int acpi_enable(void);

// Shutdown the system using ACPI (S5 state)
void acpi_shutdown(void);

// Reboot the system using ACPI or fallback methods
void acpi_reboot(void);

// Get ACPI revision (0 = 1.0, 2 = 2.0+)
uint8_t acpi_get_revision(void);

// Get ACPI state information (for debugging)
const acpi_state_t* acpi_get_state(void);

// Parse \_S5 object from DSDT to get sleep type values
// Returns: 0 on success, -1 on failure
int acpi_parse_s5(const uint8_t* dsdt, uint32_t length, uint16_t* slp_typa, uint16_t* slp_typb);

#endif // ACPI_H
