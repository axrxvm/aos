/*
 * === AOS HEADER BEGIN ===
 * ./src/dev/ata.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#include <dev/ata.h>
#include <io.h>
#include <serial.h>
#include <string.h>
#include <stdlib.h>

static int ata_initialized = 0;
static int ata_available = 0;
static uint32_t ata_total_sectors = 0;

// Wait for ATA drive to be ready
static int ata_wait_bsy(void) {
    uint32_t timeout = 10000000;  // Increased from 1000000
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;  // Not busy
        }
    }
    return -1;  // Timeout
}

// Wait for data to be ready
static int ata_wait_drq(void) {
    uint32_t timeout = 10000000;  // Increased from 1000000
    while (timeout--) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_DRQ) {
            return 0;  // Data ready
        }
        if (status & ATA_SR_ERR) {
            return -1;  // Error
        }
    }
    return -1;  // Timeout
}

// Read status and clear interrupt
static uint8_t ata_read_status(void) {
    return inb(ATA_PRIMARY_STATUS);
}

// Perform 400ns delay by reading alternate status register
static void ata_400ns_delay(void) {
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_ALT_STATUS);
    }
}

void ata_init(void) {
    serial_puts("Initializing ATA driver...\n");
    
    // Disable interrupts on ATA controller
    outb(ATA_PRIMARY_CTRL, 0x02);
    
    // Select master drive
    outb(ATA_PRIMARY_DRIVE_SELECT, ATA_MASTER);
    ata_400ns_delay();
    
    // Send IDENTIFY command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay();
    
    // Check if drive exists
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        serial_puts("ATA: No drive detected\n");
        ata_available = 0;
        ata_initialized = 1;
        return;
    }
    
    // Wait for BSY to clear
    if (ata_wait_bsy() != 0) {
        serial_puts("ATA: Drive busy timeout during init\n");
        ata_available = 0;
        ata_initialized = 1;
        return;
    }
    
    // Check status
    status = inb(ATA_PRIMARY_STATUS);
    if (status & ATA_SR_ERR) {
        serial_puts("ATA: Drive error during IDENTIFY\n");
        ata_available = 0;
        ata_initialized = 1;
        return;
    }
    
    // Wait for DRQ (data ready)
    if (ata_wait_drq() != 0) {
        serial_puts("ATA: Data not ready during IDENTIFY\n");
        ata_available = 0;
        ata_initialized = 1;
        return;
    }
    
    // Read IDENTIFY data (256 words = 512 bytes)
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(ATA_PRIMARY_DATA);
    }
    
    // Extract disk information
    // Word 60-61: Total sectors in 28-bit LBA mode
    uint32_t total_sectors = (identify_data[61] << 16) | identify_data[60];
    
    ata_total_sectors = total_sectors;
    ata_available = 1;
    ata_initialized = 1;
    
    serial_puts("ATA drive detected, ");
    serial_puts("Total sectors: ");
    char buf[32];
    itoa(total_sectors, buf, 10);
    serial_puts(buf);
    serial_puts(" (");
    itoa((total_sectors * 512) / (1024 * 1024), buf, 10);
    serial_puts(buf);
    serial_puts(" MB)\n");
    serial_puts("ATA driver initialized.\n");
}

int ata_drive_available(void) {
    return ata_available;
}

uint32_t ata_get_sector_count(void) {
    return ata_total_sectors;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!ata_initialized) {
        serial_puts("ATA: Driver not initialized\n");
        return -1;
    }
    
    if (!ata_available) {
        serial_puts("ATA: No drive available\n");
        return -1;
    }
    
    if (count == 0) {
        return 0;  // Nothing to read
    }
    
    // Wait for drive to be ready
    if (ata_wait_bsy() != 0) {
        serial_puts("ATA: Drive busy timeout (read)\n");
        return -1;
    }
    
    // Select drive and set LBA mode
    outb(ATA_PRIMARY_DRIVE_SELECT, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();
    
    // Send sector count
    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    
    // Send LBA
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    
    // Send READ command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);
    ata_400ns_delay();
    
    // Read sectors
    for (uint8_t sector = 0; sector < count; sector++) {
        // Wait for data to be ready
        if (ata_wait_drq() != 0) {
            serial_puts("ATA: Data not ready (read)\n");
            return -1;
        }
        
        // Read 256 words (512 bytes) per sector
        uint16_t* ptr = (uint16_t*)(buffer + sector * ATA_SECTOR_SIZE);
        for (int i = 0; i < 256; i++) {
            ptr[i] = inw(ATA_PRIMARY_DATA);
        }
        
        // Check for errors
        uint8_t status = ata_read_status();
        if (status & ATA_SR_ERR) {
            serial_puts("ATA: Read error\n");
            return -1;
        }
    }
    
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    if (!ata_initialized) {
        serial_puts("ATA: Driver not initialized\n");
        return -1;
    }
    
    if (!ata_available) {
        serial_puts("ATA: No drive available\n");
        return -1;
    }
    
    if (count == 0) {
        return 0;  // Nothing to write
    }
    
    // Wait for drive to be ready
    if (ata_wait_bsy() != 0) {
        serial_puts("ATA: Drive busy timeout (write)\n");
        return -1;
    }
    
    // Select drive and set LBA mode
    outb(ATA_PRIMARY_DRIVE_SELECT, 0xE0 | ((lba >> 24) & 0x0F));
    ata_400ns_delay();
    
    // Wait for drive after selection
    if (ata_wait_bsy() != 0) {
        serial_puts("ATA: Drive busy after selection (write)\n");
        return -1;
    }
    
    // Send sector count
    outb(ATA_PRIMARY_SECTOR_COUNT, count);
    
    // Send LBA
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    
    // Send WRITE command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);
    ata_400ns_delay();
    
    // Write sectors
    for (uint8_t sector = 0; sector < count; sector++) {
        // Wait for drive to be ready for data
        if (ata_wait_drq() != 0) {
            serial_puts("ATA: Data not ready for sector ");
            char buf[16];
            itoa(sector, buf, 10);
            serial_puts(buf);
            serial_puts(" (write)\n");
            return -1;
        }
        
        // Write 256 words (512 bytes) per sector
        const uint16_t* ptr = (const uint16_t*)(buffer + sector * ATA_SECTOR_SIZE);
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, ptr[i]);
        }
        
        // Wait for write to complete
        if (ata_wait_bsy() != 0) {
            serial_puts("ATA: Busy timeout after write\n");
            return -1;
        }
        
        // Check for errors
        uint8_t status = ata_read_status();
        if (status & ATA_SR_ERR) {
            serial_puts("ATA: Write error\n");
            return -1;
        }
    }
    
    // Flush cache (non-fatal if not supported by emulated drives)
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_400ns_delay();
    
    if (ata_wait_bsy() != 0) {
        serial_puts("ATA: Cache flush not supported (ignored)\n");
        // Don't fail - many emulated drives don't support cache flush
    }
    
    return 0;
}
