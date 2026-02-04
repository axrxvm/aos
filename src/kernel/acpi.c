/*
 * === AOS HEADER BEGIN ===
 * ./src/kernel/acpi.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


/* 
 * ACPI (Advanced Configuration and Power Interface) Implementation
 * Provides system power management and shutdown capabilities
 */

#include <acpi.h>
#include <io.h>
#include <serial.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <arch_paging.h>

// ACPI state structure
static acpi_state_t acpi_state = {
    .initialized = false,
    .enabled = false,
    .revision = 0,
    .pm1a_control = 0,
    .pm1b_control = 0,
    .slp_typa = 0,
    .slp_typb = 0,
    .smi_cmd = 0,
    .acpi_enable_val = 0,
    .fadt = NULL,
    .rsdt = NULL
};

// Memory regions to search for RSDP
// EBDA (Extended BIOS Data Area) - typically at segment pointed to by 0x40:0x0E
// Main BIOS area: 0x000E0000 - 0x000FFFFF
#define EBDA_START      0x00080000
#define EBDA_END        0x000A0000
#define BIOS_START      0x000E0000
#define BIOS_END        0x00100000

// Maximum address we can safely access (first 8MB is identity mapped)
#define MAX_SAFE_ADDR   0x00800000

// Helper to check if an address range is safely accessible
static bool is_safe_address(uint32_t addr, uint32_t size) {
    // Check if within identity-mapped region or BIOS area
    if (addr < MAX_SAFE_ADDR && (addr + size) < MAX_SAFE_ADDR) {
        return true;
    }
    // BIOS area is special-cased in most x86 setups
    if (addr >= 0xE0000 && (addr + size) <= 0x100000) {
        return true;
    }
    return false;
}

// Validate checksum for ACPI structures
static bool validate_checksum(const void* ptr, size_t length) {
    const uint8_t* bytes = (const uint8_t*)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

// Search for RSDP in a memory region
static rsdp_t* find_rsdp_in_region(uint32_t start, uint32_t end) {
    // RSDP must be 16-byte aligned
    for (uint32_t addr = start; addr < end; addr += 16) {
        rsdp_t* rsdp = (rsdp_t*)addr;
        
        // Check signature
        if (memcmp(rsdp->signature, RSDP_SIGNATURE, RSDP_SIGNATURE_LEN) == 0) {
            // Validate checksum (first 20 bytes for ACPI 1.0)
            if (validate_checksum(rsdp, 20)) {
                serial_puts("ACPI: Found RSDP at 0x");
                char buf[16];
                itoa(addr, buf, 16);
                serial_puts(buf);
                serial_puts("\n");
                return rsdp;
            }
        }
    }
    return NULL;
}

// Find RSDP (Root System Description Pointer)
static rsdp_t* find_rsdp(void) {
    rsdp_t* rsdp = NULL;
    
    // First, try EBDA (get address from BDA at 0x40:0x0E = physical 0x40E)
    // Read BDA EBDA pointer using direct memory read to avoid bounds checking
    uint16_t ebda_segment;
    // Use inline assembly to read from low memory, avoiding GCC array bounds warnings
    __asm__ volatile (
        "movw 0x40E, %%ax\n"
        "movw %%ax, %0\n"
        : "=r"(ebda_segment)
        :
        : "ax"
    );
    uint32_t ebda_addr = (uint32_t)ebda_segment << 4;
    
    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000) {
        serial_puts("ACPI: Searching EBDA at 0x");
        char buf[16];
        itoa(ebda_addr, buf, 16);
        serial_puts(buf);
        serial_puts("\n");
        
        rsdp = find_rsdp_in_region(ebda_addr, ebda_addr + 1024);
        if (rsdp) return rsdp;
    }
    
    // Search main BIOS area
    serial_puts("ACPI: Searching BIOS region 0xE0000-0xFFFFF\n");
    rsdp = find_rsdp_in_region(BIOS_START, BIOS_END);
    
    return rsdp;
}

// Parse the \_S5 object from DSDT to get sleep type values
// This is a simplified parser that looks for the _S5_ pattern
int acpi_parse_s5(const uint8_t* dsdt, uint32_t length, uint16_t* slp_typa, uint16_t* slp_typb) {
    if (!dsdt || length < sizeof(acpi_header_t)) {
        return -1;
    }
    
    // Skip DSDT header
    const uint8_t* ptr = dsdt + sizeof(acpi_header_t);
    const uint8_t* end = dsdt + length - 5;
    
    // Search for "_S5_" in the DSDT
    // Format typically: _S5_ followed by package bytes
    while (ptr < end) {
        if (ptr[0] == '_' && ptr[1] == 'S' && ptr[2] == '5' && ptr[3] == '_') {
            serial_puts("ACPI: Found _S5_ object\n");
            
            // Move past "_S5_"
            ptr += 4;
            
            // Skip to package opcode (0x12)
            while (ptr < end && *ptr != 0x12) {
                ptr++;
            }
            
            if (ptr >= end) break;
            
            // Skip package opcode
            ptr++;
            
            // Parse package length (simplified - handle 1-byte length)
            uint8_t pkg_len = *ptr;
            if (pkg_len & 0xC0) {
                // Multi-byte length, skip for now
                ptr += (pkg_len >> 6) + 1;
            } else {
                ptr++;
            }
            
            // Skip num elements byte
            ptr++;
            
            // Now we should have the SLP_TYPa and SLP_TYPb values
            // They can be ByteConst (0x0A XX) or plain bytes
            
            if (*ptr == 0x0A) {
                ptr++;
                *slp_typa = *ptr;
                ptr++;
            } else {
                *slp_typa = *ptr;
                ptr++;
            }
            
            if (*ptr == 0x0A) {
                ptr++;
                *slp_typb = *ptr;
            } else {
                *slp_typb = *ptr;
            }
            
            serial_puts("ACPI: SLP_TYPa=0x");
            char buf[8];
            itoa(*slp_typa, buf, 16);
            serial_puts(buf);
            serial_puts(", SLP_TYPb=0x");
            itoa(*slp_typb, buf, 16);
            serial_puts(buf);
            serial_puts("\n");
            
            return 0;
        }
        ptr++;
    }
    
    // Default S5 values if _S5_ not found
    serial_puts("ACPI: _S5_ not found, using defaults\n");
    *slp_typa = 5;  // Common default for S5
    *slp_typb = 0;
    
    return 0;
}

// Initialize ACPI subsystem
int acpi_init(void) {
    serial_puts("ACPI: Initializing...\n");
    
    // Find RSDP
    rsdp_t* rsdp = find_rsdp();
    if (!rsdp) {
        serial_puts("ACPI: RSDP not found, will use fallback shutdown methods\n");
        return -1;
    }
    
    acpi_state.revision = rsdp->revision;
    serial_puts("ACPI: Revision ");
    char buf[16];
    itoa(rsdp->revision, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    // Check if RSDT address is accessible
    uint32_t rsdt_addr = rsdp->rsdt_address;
    serial_puts("ACPI: RSDT pointer at 0x");
    itoa(rsdt_addr, buf, 16);
    serial_puts(buf);
    serial_puts("\n");
    
    // If RSDT is in high memory (common in QEMU/Bochs), we can't access it
    // without additional page mapping. Use fallback methods instead.
    if (!is_safe_address(rsdt_addr, sizeof(rsdt_t))) {
        serial_puts("ACPI: RSDT at high memory (0x");
        itoa(rsdt_addr, buf, 16);
        serial_puts(buf);
        serial_puts("), using fallback shutdown\n");
        
        // Set up fallback - we found RSDP so ACPI exists, but tables are inaccessible
        // The QEMU/Bochs/VirtualBox fallback ports will work
        acpi_state.initialized = true;
        acpi_state.enabled = true;
        return 0;
    }
    
    // Get RSDT
    rsdt_t* rsdt = (rsdt_t*)(uintptr_t)rsdt_addr;
    if (!validate_checksum(rsdt, rsdt->header.length)) {
        serial_puts("ACPI: Invalid RSDT checksum\n");
        return -1;
    }
    acpi_state.rsdt = rsdt;
    
    serial_puts("ACPI: RSDT at 0x");
    itoa(rsdp->rsdt_address, buf, 16);
    serial_puts(buf);
    serial_puts(", length ");
    itoa(rsdt->header.length, buf, 10);
    serial_puts(buf);
    serial_puts("\n");
    
    // Find FADT (FACP) - check if table addresses are accessible
    uint32_t num_entries = (rsdt->header.length - sizeof(acpi_header_t)) / sizeof(uint32_t);
    fadt_t* fadt = NULL;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t table_addr = rsdt->entry[i];
        if (!is_safe_address(table_addr, sizeof(acpi_header_t))) {
            continue;  // Skip inaccessible tables
        }
        
        acpi_header_t* header = (acpi_header_t*)(uintptr_t)table_addr;
        if (memcmp(header->signature, FACP_SIGNATURE, 4) == 0) {
            if (validate_checksum(header, header->length)) {
                serial_puts("ACPI: Found FADT\n");
                fadt = (fadt_t*)header;
                break;
            }
        }
    }
    
    if (!fadt) {
        serial_puts("ACPI: FADT not found or inaccessible, using fallback\n");
        acpi_state.initialized = true;
        acpi_state.enabled = true;
        return 0;
    }
    acpi_state.fadt = fadt;
    
    // Extract PM control ports
    acpi_state.pm1a_control = fadt->pm1a_control_block;
    acpi_state.pm1b_control = fadt->pm1b_control_block;
    acpi_state.smi_cmd = fadt->smi_command_port;
    acpi_state.acpi_enable_val = fadt->acpi_enable;
    
    serial_puts("ACPI: PM1a_CNT=0x");
    itoa(acpi_state.pm1a_control, buf, 16);
    serial_puts(buf);
    if (acpi_state.pm1b_control) {
        serial_puts(", PM1b_CNT=0x");
        itoa(acpi_state.pm1b_control, buf, 16);
        serial_puts(buf);
    }
    serial_puts("\n");
    
    serial_puts("ACPI: SMI_CMD=0x");
    itoa(acpi_state.smi_cmd, buf, 16);
    serial_puts(buf);
    serial_puts(", ACPI_ENABLE=0x");
    itoa(acpi_state.acpi_enable_val, buf, 16);
    serial_puts(buf);
    serial_puts("\n");
    
    // Parse DSDT to get S5 sleep type values (if accessible)
    if (fadt->dsdt && is_safe_address(fadt->dsdt, sizeof(acpi_header_t))) {
        acpi_header_t* dsdt = (acpi_header_t*)(uintptr_t)fadt->dsdt;
        if (memcmp(dsdt->signature, DSDT_SIGNATURE, 4) == 0 && 
            is_safe_address(fadt->dsdt, dsdt->length)) {
            acpi_parse_s5((uint8_t*)dsdt, dsdt->length, 
                         &acpi_state.slp_typa, &acpi_state.slp_typb);
        } else {
            serial_puts("ACPI: DSDT inaccessible, using default S5 values\n");
            acpi_state.slp_typa = 5;
            acpi_state.slp_typb = 0;
        }
    } else {
        serial_puts("ACPI: No DSDT or inaccessible, using default S5 values\n");
        acpi_state.slp_typa = 5;
        acpi_state.slp_typb = 0;
    }
    
    acpi_state.initialized = true;
    serial_puts("ACPI: Initialized successfully\n");
    
    return 0;
}

// Enable ACPI mode
int acpi_enable(void) {
    if (!acpi_state.initialized) {
        return -1;
    }
    
    // Check if already in ACPI mode
    if (acpi_state.pm1a_control) {
        uint16_t pm1a_val = inw(acpi_state.pm1a_control);
        if (pm1a_val & 0x0001) {  // SCI_EN bit
            serial_puts("ACPI: Already enabled\n");
            acpi_state.enabled = true;
            return 0;
        }
    }
    
    // Enable ACPI by writing to SMI command port
    if (acpi_state.smi_cmd && acpi_state.acpi_enable_val) {
        serial_puts("ACPI: Enabling via SMI command\n");
        outb(acpi_state.smi_cmd, acpi_state.acpi_enable_val);
        
        // Wait for ACPI to become enabled (with timeout)
        for (int i = 0; i < 1000; i++) {
            if (acpi_state.pm1a_control) {
                uint16_t pm1a_val = inw(acpi_state.pm1a_control);
                if (pm1a_val & 0x0001) {
                    serial_puts("ACPI: Enabled successfully\n");
                    acpi_state.enabled = true;
                    return 0;
                }
            }
            // Small delay
            for (volatile int j = 0; j < 10000; j++);
        }
        serial_puts("ACPI: Enable timeout\n");
    }
    
    // Assume enabled if we have PM control port
    if (acpi_state.pm1a_control) {
        acpi_state.enabled = true;
        return 0;
    }
    
    return -1;
}

// Check if ACPI is available
bool acpi_available(void) {
    return acpi_state.initialized;
}

// Get ACPI revision
uint8_t acpi_get_revision(void) {
    return acpi_state.revision;
}

// Get ACPI state
const acpi_state_t* acpi_get_state(void) {
    return &acpi_state;
}

// Shutdown using ACPI
void acpi_shutdown(void) {
    serial_puts("ACPI: Initiating shutdown (S5)...\n");
    
    // Disable interrupts
    asm volatile("cli");
    
    if (acpi_state.initialized && acpi_state.pm1a_control) {
        // Enable ACPI if not already enabled
        if (!acpi_state.enabled) {
            acpi_enable();
        }
        
        // Prepare SLP_TYPx and SLP_EN values
        uint16_t slp_val_a = (acpi_state.slp_typa << PM1_CNT_SLP_TYP_SHIFT) | PM1_CNT_SLP_EN;
        uint16_t slp_val_b = (acpi_state.slp_typb << PM1_CNT_SLP_TYP_SHIFT) | PM1_CNT_SLP_EN;
        
        serial_puts("ACPI: Writing to PM1a_CNT: 0x");
        char buf[16];
        itoa(slp_val_a, buf, 16);
        serial_puts(buf);
        serial_puts("\n");
        
        // Write to PM1a_CNT
        outw(acpi_state.pm1a_control, slp_val_a);
        
        // Write to PM1b_CNT if present
        if (acpi_state.pm1b_control) {
            serial_puts("ACPI: Writing to PM1b_CNT: 0x");
            itoa(slp_val_b, buf, 16);
            serial_puts(buf);
            serial_puts("\n");
            outw(acpi_state.pm1b_control, slp_val_b);
        }
        
        // Give hardware time to process
        for (volatile int i = 0; i < 100000; i++);
    }
    
    // Fallback: Try common hypervisor/emulator shutdown ports
    serial_puts("ACPI: Trying QEMU shutdown port...\n");
    outw(QEMU_ACPI_SHUTDOWN_PORT, QEMU_ACPI_SHUTDOWN_VALUE);
    
    for (volatile int i = 0; i < 100000; i++);
    
    serial_puts("ACPI: Trying Bochs shutdown port...\n");
    outw(BOCHS_ACPI_PORT, BOCHS_ACPI_SHUTDOWN_VAL);
    
    for (volatile int i = 0; i < 100000; i++);
    
    serial_puts("ACPI: Trying VirtualBox shutdown port...\n");
    outw(VIRTUALBOX_ACPI_PORT, VIRTUALBOX_SHUTDOWN_VAL);
    
    // If we're still here, shutdown failed - halt
    serial_puts("ACPI: Shutdown failed, halting CPU\n");
    while (1) {
        asm volatile("hlt");
    }
}

// Reboot using various methods
void acpi_reboot(void) {
    serial_puts("ACPI: Initiating reboot...\n");
    
    // Disable interrupts
    asm volatile("cli");
    
    // Method 1: Keyboard controller reset
    serial_puts("ACPI: Trying keyboard controller reset\n");
    
    // Wait for keyboard controller to be ready
    uint8_t temp;
    do {
        temp = inb(0x64);
        if (temp & 0x01) {
            inb(0x60);  // Flush output buffer
        }
    } while (temp & 0x02);
    
    // Send reset command
    outb(0x64, 0xFE);
    
    // Small delay
    for (volatile int i = 0; i < 100000; i++);
    
    // Method 2: Triple fault (causes CPU reset)
    serial_puts("ACPI: Keyboard reset failed, trying triple fault\n");
    
    // Load a null IDT and trigger an interrupt
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    
    asm volatile("lidt %0" : : "m"(null_idt));
    asm volatile("int $0x03");  // This should triple fault
    
    // If we're still here, halt
    while (1) {
        asm volatile("hlt");
    }
}
