/*
 * === AOS HEADER BEGIN ===
 * src/kernel/elf.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */


#include <elf.h>
#include <fs/vfs.h>
#include <vmm.h>
#include <process.h>
#include <string.h>
#include <serial.h>

// Validate ELF header
int elf_validate(const void* elf_data) {
    if (!elf_data) {
        return -1;
    }
    
    elf32_header_t* header = (elf32_header_t*)elf_data;
    
    // Check magic number
    if (header->e_ident[0] != 0x7F ||
        header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' ||
        header->e_ident[3] != 'F') {
        serial_puts("ELF: Invalid magic number\n");
        return -1;
    }
    
    // Check 32-bit
    if (header->e_ident[4] != 1) {
        serial_puts("ELF: Not 32-bit\n");
        return -1;
    }
    
    // Check little-endian
    if (header->e_ident[5] != 1) {
        serial_puts("ELF: Not little-endian\n");
        return -1;
    }
    
    // Check version
    if (header->e_ident[6] != 1) {
        serial_puts("ELF: Invalid version\n");
        return -1;
    }
    
    // Check executable type
    if (header->e_type != ET_EXEC) {
        serial_puts("ELF: Not executable\n");
        return -1;
    }
    
    // Check architecture (i386)
    if (header->e_machine != EM_386) {
        serial_puts("ELF: Not i386\n");
        return -1;
    }
    
    return 0;
}

// Load ELF from memory
int elf_load_from_memory(const void* elf_data, uint32_t size, uint32_t* entry_point) {
    (void)size;  // Size validation could be added later
    
    if (!elf_data || !entry_point) {
        return -1;
    }
    
    // Validate header
    if (elf_validate(elf_data) != 0) {
        return -1;
    }
    
    elf32_header_t* header = (elf32_header_t*)elf_data;
    process_t* current = process_get_current();
    
    if (!current || !current->address_space) {
        serial_puts("ELF: No current process\n");
        return -1;
    }
    
    // Get entry point
    *entry_point = header->e_entry;
    
    // Load program headers
    elf32_program_header_t* pheader = (elf32_program_header_t*)((uint32_t)elf_data + header->e_phoff);
    
    for (int i = 0; i < header->e_phnum; i++) {
        if (pheader[i].p_type == PT_LOAD) {
            // Calculate page-aligned addresses
            uint32_t vaddr = pheader[i].p_vaddr & ~0xFFF;  // Page align down
            uint32_t vaddr_end = (pheader[i].p_vaddr + pheader[i].p_memsz + 0xFFF) & ~0xFFF;
            uint32_t pages = (vaddr_end - vaddr) / 4096;
            
            // Determine flags
            uint32_t flags = VMM_PRESENT | VMM_USER;
            if (pheader[i].p_flags & PF_W) {
                flags |= VMM_WRITE;
            }
            
            // Allocate memory for segment
            if (!vmm_alloc_at(current->address_space, vaddr, pages * 4096, flags)) {
                serial_puts("ELF: Failed to allocate memory for segment\n");
                return -1;
            }
            
            // Copy segment data
            if (pheader[i].p_filesz > 0) {
                uint32_t src = (uint32_t)elf_data + pheader[i].p_offset;
                uint32_t dst = pheader[i].p_vaddr;
                memcpy((void*)dst, (void*)src, pheader[i].p_filesz);
            }
            
            // Zero BSS section
            if (pheader[i].p_memsz > pheader[i].p_filesz) {
                uint32_t bss_start = pheader[i].p_vaddr + pheader[i].p_filesz;
                uint32_t bss_size = pheader[i].p_memsz - pheader[i].p_filesz;
                memset((void*)bss_start, 0, bss_size);
            }
        }
    }
    
    return 0;
}

// Load ELF from file
int elf_load(const char* path, uint32_t* entry_point) {
    if (!path || !entry_point) {
        return -1;
    }
    
    // Open file
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_puts("ELF: Failed to open file\n");
        return -1;
    }
    
    // Get file size
    stat_t stat;
    if (vfs_stat(path, &stat) != 0) {
        vfs_close(fd);
        serial_puts("ELF: Failed to stat file\n");
        return -1;
    }
    
    // Allocate buffer
    void* buffer = kmalloc(stat.st_size);
    if (!buffer) {
        vfs_close(fd);
        serial_puts("ELF: Failed to allocate buffer\n");
        return -1;
    }
    
    // Read file
    int bytes_read = vfs_read(fd, buffer, stat.st_size);
    vfs_close(fd);
    
    if (bytes_read != (int)stat.st_size) {
        kfree(buffer);
        serial_puts("ELF: Failed to read file\n");
        return -1;
    }
    
    // Load from memory
    int result = elf_load_from_memory(buffer, stat.st_size, entry_point);
    
    // Free buffer
    kfree(buffer);
    
    return result;
}
