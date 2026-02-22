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

static int elf_validate_ident(const uint8_t* ident) {
    if (!ident) {
        return -1;
    }

    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        serial_puts("ELF: Invalid magic number\n");
        return -1;
    }

    if (ident[5] != ELF_DATA_LSB) {
        serial_puts("ELF: Not little-endian\n");
        return -1;
    }

    if (ident[6] != 1) {
        serial_puts("ELF: Invalid version\n");
        return -1;
    }

    return 0;
}

static int elf_load_segments_32(const uint8_t* elf_data, size_t size, process_t* current) {
    const elf32_header_t* header = (const elf32_header_t*)elf_data;
    if ((size_t)header->e_phoff > size) {
        serial_puts("ELF32: Program header offset out of range\n");
        return -1;
    }
    if ((size_t)header->e_phnum > 0 &&
        ((size_t)header->e_phentsize == 0 ||
         (size_t)header->e_phnum > (SIZE_MAX / (size_t)header->e_phentsize))) {
        serial_puts("ELF32: Invalid program header table size\n");
        return -1;
    }

    size_t ph_table_size = (size_t)header->e_phnum * (size_t)header->e_phentsize;
    if ((size_t)header->e_phoff + ph_table_size > size) {
        serial_puts("ELF32: Program header table exceeds file size\n");
        return -1;
    }

    const uint8_t* ph_table = elf_data + header->e_phoff;
    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf32_program_header_t* ph =
            (const elf32_program_header_t*)(ph_table + ((size_t)i * header->e_phentsize));

        if (ph->p_type != PT_LOAD) {
            continue;
        }

        uintptr_t seg_vaddr = (uintptr_t)ph->p_vaddr;
        uintptr_t seg_memsz = (uintptr_t)ph->p_memsz;
        uintptr_t seg_filesz = (uintptr_t)ph->p_filesz;

        if (seg_memsz < seg_filesz) {
            serial_puts("ELF32: Segment memsz < filesz\n");
            return -1;
        }
        if ((size_t)ph->p_offset > size || (size_t)ph->p_filesz > size - (size_t)ph->p_offset) {
            serial_puts("ELF32: Segment data out of range\n");
            return -1;
        }
        if (seg_vaddr + seg_memsz < seg_vaddr) {
            serial_puts("ELF32: Segment address overflow\n");
            return -1;
        }

        uintptr_t vaddr_start = seg_vaddr & ~(uintptr_t)(PAGE_SIZE - 1);
        uintptr_t vaddr_end = (seg_vaddr + seg_memsz + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
        if (vaddr_end < vaddr_start) {
            serial_puts("ELF32: Segment page range overflow\n");
            return -1;
        }

        uint32_t flags = VMM_PRESENT | VMM_USER;
        if (ph->p_flags & PF_W) {
            flags |= VMM_WRITE;
        }

        if (!vmm_alloc_at(current->address_space, vaddr_start, (size_t)(vaddr_end - vaddr_start), flags)) {
            serial_puts("ELF32: Failed to allocate memory for segment\n");
            return -1;
        }

        if (ph->p_filesz > 0) {
            memcpy((void*)seg_vaddr, elf_data + ph->p_offset, ph->p_filesz);
        }

        if (ph->p_memsz > ph->p_filesz) {
            memset((void*)(seg_vaddr + seg_filesz), 0, (size_t)(seg_memsz - seg_filesz));
        }
    }

    return 0;
}

static int elf_load_segments_64(const uint8_t* elf_data, size_t size, process_t* current) {
    const elf64_header_t* header = (const elf64_header_t*)elf_data;
    if ((size_t)header->e_phoff > size) {
        serial_puts("ELF64: Program header offset out of range\n");
        return -1;
    }
    if ((size_t)header->e_phnum > 0 &&
        ((size_t)header->e_phentsize == 0 ||
         (size_t)header->e_phnum > (SIZE_MAX / (size_t)header->e_phentsize))) {
        serial_puts("ELF64: Invalid program header table size\n");
        return -1;
    }

    size_t ph_table_size = (size_t)header->e_phnum * (size_t)header->e_phentsize;
    if ((size_t)header->e_phoff + ph_table_size > size) {
        serial_puts("ELF64: Program header table exceeds file size\n");
        return -1;
    }

    const uint8_t* ph_table = elf_data + (size_t)header->e_phoff;
    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const elf64_program_header_t* ph =
            (const elf64_program_header_t*)(ph_table + ((size_t)i * header->e_phentsize));

        if (ph->p_type != PT_LOAD) {
            continue;
        }

        uintptr_t seg_vaddr = (uintptr_t)ph->p_vaddr;
        uintptr_t seg_memsz = (uintptr_t)ph->p_memsz;
        uintptr_t seg_filesz = (uintptr_t)ph->p_filesz;

        if ((uint64_t)seg_vaddr != ph->p_vaddr || (uint64_t)seg_memsz != ph->p_memsz ||
            (uint64_t)seg_filesz != ph->p_filesz) {
            serial_puts("ELF64: Segment values exceed native pointer width\n");
            return -1;
        }
        if (seg_memsz < seg_filesz) {
            serial_puts("ELF64: Segment memsz < filesz\n");
            return -1;
        }
        if ((size_t)ph->p_offset > size || (size_t)ph->p_filesz > size - (size_t)ph->p_offset) {
            serial_puts("ELF64: Segment data out of range\n");
            return -1;
        }
        if (seg_vaddr + seg_memsz < seg_vaddr) {
            serial_puts("ELF64: Segment address overflow\n");
            return -1;
        }

        uintptr_t vaddr_start = seg_vaddr & ~(uintptr_t)(PAGE_SIZE - 1);
        uintptr_t vaddr_end = (seg_vaddr + seg_memsz + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
        if (vaddr_end < vaddr_start) {
            serial_puts("ELF64: Segment page range overflow\n");
            return -1;
        }

        uint32_t flags = VMM_PRESENT | VMM_USER;
        if (ph->p_flags & PF_W) {
            flags |= VMM_WRITE;
        }

        if (!vmm_alloc_at(current->address_space, vaddr_start, (size_t)(vaddr_end - vaddr_start), flags)) {
            serial_puts("ELF64: Failed to allocate memory for segment\n");
            return -1;
        }

        if (ph->p_filesz > 0) {
            memcpy((void*)seg_vaddr, elf_data + (size_t)ph->p_offset, (size_t)ph->p_filesz);
        }

        if (ph->p_memsz > ph->p_filesz) {
            memset((void*)(seg_vaddr + seg_filesz), 0, (size_t)(seg_memsz - seg_filesz));
        }
    }

    return 0;
}

// Validate ELF header
int elf_validate(const void* elf_data) {
    if (!elf_data) {
        return -1;
    }

    const uint8_t* ident = (const uint8_t*)elf_data;
    if (elf_validate_ident(ident) != 0) {
        return -1;
    }

    if (ident[4] == ELF_CLASS_32) {
        const elf32_header_t* header = (const elf32_header_t*)elf_data;
        if (header->e_type != ET_EXEC) {
            serial_puts("ELF32: Not executable\n");
            return -1;
        }
#ifdef ARCH_X86_64
        serial_puts("ELF32: 32-bit user binaries are unsupported on x86_64 kernel\n");
        return -1;
#else
        if (header->e_machine != EM_386) {
            serial_puts("ELF32: Unsupported machine\n");
            return -1;
        }
#endif
        return 0;
    }

    if (ident[4] == ELF_CLASS_64) {
        const elf64_header_t* header = (const elf64_header_t*)elf_data;
        if (header->e_type != ET_EXEC) {
            serial_puts("ELF64: Not executable\n");
            return -1;
        }
#ifdef ARCH_X86_64
        if (header->e_machine != EM_X86_64) {
            serial_puts("ELF64: Unsupported machine\n");
            return -1;
        }
        return 0;
#else
        serial_puts("ELF64: 64-bit binaries are unsupported on this kernel\n");
        return -1;
#endif
    }

    serial_puts("ELF: Unsupported ELF class\n");
    return -1;
}

// Load ELF from memory
int elf_load_from_memory(const void* elf_data, size_t size, uintptr_t* entry_point) {
    if (!elf_data || !entry_point) {
        return -1;
    }

    // Validate header
    if (elf_validate(elf_data) != 0) {
        return -1;
    }

    process_t* current = process_get_current();
    if (!current || !current->address_space) {
        serial_puts("ELF: No current process\n");
        return -1;
    }

    const uint8_t* ident = (const uint8_t*)elf_data;
    if (ident[4] == ELF_CLASS_32) {
        const elf32_header_t* header = (const elf32_header_t*)elf_data;
        *entry_point = (uintptr_t)header->e_entry;
        return elf_load_segments_32((const uint8_t*)elf_data, size, current);
    }

    if (ident[4] == ELF_CLASS_64) {
        const elf64_header_t* header = (const elf64_header_t*)elf_data;
        *entry_point = (uintptr_t)header->e_entry;
        return elf_load_segments_64((const uint8_t*)elf_data, size, current);
    }

    serial_puts("ELF: Unsupported class during load\n");
    return -1;
}

// Load ELF from file
int elf_load(const char* path, uintptr_t* entry_point) {
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
    int result = elf_load_from_memory(buffer, (size_t)stat.st_size, entry_point);
    
    // Free buffer
    kfree(buffer);
    
    return result;
}
