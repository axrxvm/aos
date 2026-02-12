/*
 * === AOS HEADER BEGIN ===
 * src/dev/pci.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <dev/pci.h>
#include <io.h>
#include <serial.h>
#include <stdlib.h>
#include <vmm.h>

#define MAX_PCI_DEVICES 32

static pci_device_t pci_devices[MAX_PCI_DEVICES];
static int pci_device_count = 0;

uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | 
                                  (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | 
                                  (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t data = pci_read_config(bus, device, function, offset & 0xFC);
    return (uint16_t)((data >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t data = pci_read_config(bus, device, function, offset & 0xFC);
    return (uint8_t)((data >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t data = pci_read_config(bus, device, function, offset & 0xFC);
    uint32_t shift = (offset & 2) * 8;
    data = (data & ~(0xFFFF << shift)) | ((uint32_t)value << shift);
    pci_write_config(bus, device, function, offset & 0xFC, data);
}

int pci_scan_bus(void) {
    pci_device_count = 0;
    
    // Only scan bus 0 for now (most devices are on bus 0)
    for (uint16_t bus = 0; bus < 1; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor_id = pci_read_config_word(bus, device, function, PCI_VENDOR_ID);
                
                if (vendor_id == 0xFFFF) {
                    continue;  // No device
                }
                
                if (pci_device_count >= MAX_PCI_DEVICES) {
                    serial_puts("PCI: Maximum device limit reached\n");
                    return pci_device_count;
                }
                
                pci_device_t* dev = &pci_devices[pci_device_count++];
                dev->bus = bus;
                dev->device = device;
                dev->function = function;
                dev->vendor_id = vendor_id;
                dev->device_id = pci_read_config_word(bus, device, function, PCI_DEVICE_ID);
                dev->class_code = pci_read_config_byte(bus, device, function, PCI_CLASS_CODE);
                dev->subclass = pci_read_config_byte(bus, device, function, PCI_SUBCLASS);
                dev->prog_if = pci_read_config_byte(bus, device, function, PCI_PROG_IF);
                dev->revision = pci_read_config_byte(bus, device, function, PCI_REVISION_ID);
                dev->interrupt_line = pci_read_config_byte(bus, device, function, PCI_INTERRUPT_LINE);
                
                // Read BARs
                for (int i = 0; i < 6; i++) {
                    dev->bar[i] = pci_read_config(bus, device, function, PCI_BAR0 + (i * 4));
                }
                
                serial_puts("PCI: Found device ");
                char buf[16];
                itoa(vendor_id, buf, 16);
                serial_puts(buf);
                serial_puts(":");
                itoa(dev->device_id, buf, 16);
                serial_puts(buf);
                serial_puts("\n");
            }
        }
    }
    
    return pci_device_count;
}

pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && 
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

void pci_init(void) {
    serial_puts("Initializing PCI subsystem...\n");
    int count = pci_scan_bus();
    serial_puts("PCI: Found ");
    char buf[16];
    itoa(count, buf, 10);
    serial_puts(buf);
    serial_puts(" devices\n");
}
