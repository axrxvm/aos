/*
 * === AOS HEADER BEGIN ===
 * ./include/dev/ata.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef ATA_H
#define ATA_H

#include <stdint.h>

// ATA/IDE Primary Bus I/O Ports
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERROR        0x1F1
#define ATA_PRIMARY_FEATURES     0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRIVE_SELECT 0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND      0x1F7
#define ATA_PRIMARY_ALT_STATUS   0x3F6
#define ATA_PRIMARY_CTRL         0x3F6

// ATA Status Register Bits
#define ATA_SR_BSY  0x80  // Busy
#define ATA_SR_DRDY 0x40  // Drive ready
#define ATA_SR_DF   0x20  // Drive write fault
#define ATA_SR_DSC  0x10  // Drive seek complete
#define ATA_SR_DRQ  0x08  // Data request ready
#define ATA_SR_CORR 0x04  // Corrected data
#define ATA_SR_IDX  0x02  // Index
#define ATA_SR_ERR  0x01  // Error

// ATA Commands
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

// Drive Selection
#define ATA_MASTER 0xA0
#define ATA_SLAVE  0xB0

// Block size
#define ATA_SECTOR_SIZE 512

// Initialize ATA driver
void ata_init(void);

// Read sectors from disk
// lba: Logical Block Address (28-bit LBA)
// count: Number of sectors to read (1-255)
// buffer: Buffer to store read data (must be at least count * 512 bytes)
// Returns: 0 on success, -1 on error
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);

// Write sectors to disk
// lba: Logical Block Address (28-bit LBA)
// count: Number of sectors to write (1-255)
// buffer: Buffer containing data to write (must be at least count * 512 bytes)
// Returns: 0 on success, -1 on error
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);

// Check if ATA drive is available
int ata_drive_available(void);

// Get total sector count of the disk
uint32_t ata_get_sector_count(void);

#endif // ATA_H
