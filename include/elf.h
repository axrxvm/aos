/*
 * === AOS HEADER BEGIN ===
 * ./include/elf.h
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.8.5
 * === AOS HEADER END ===
 */


#ifndef ELF_H
#define ELF_H

#include <stdint.h>

// ELF Magic number
#define ELF_MAGIC 0x464C457F  // 0x7F 'E' 'L' 'F'

// ELF File types
#define ET_NONE     0  // No file type
#define ET_REL      1  // Relocatable file
#define ET_EXEC     2  // Executable file
#define ET_DYN      3  // Shared object file
#define ET_CORE     4  // Core file

// ELF Machine types
#define EM_NONE     0  // No machine
#define EM_386      3  // Intel 80386
#define EM_ARM      40 // ARM
#define EM_X86_64   62 // AMD x86-64

// Program header types
#define PT_NULL     0  // Unused entry
#define PT_LOAD     1  // Loadable segment
#define PT_DYNAMIC  2  // Dynamic linking information
#define PT_INTERP   3  // Interpreter path
#define PT_NOTE     4  // Auxiliary information
#define PT_SHLIB    5  // Reserved
#define PT_PHDR     6  // Program header table

// Program header flags
#define PF_X        0x1  // Execute
#define PF_W        0x2  // Write
#define PF_R        0x4  // Read

// Section header types
#define SHT_NULL        0  // Unused section
#define SHT_PROGBITS    1  // Program data
#define SHT_SYMTAB      2  // Symbol table
#define SHT_STRTAB      3  // String table
#define SHT_RELA        4  // Relocation entries with addends
#define SHT_HASH        5  // Symbol hash table
#define SHT_DYNAMIC     6  // Dynamic linking information
#define SHT_NOTE        7  // Notes
#define SHT_NOBITS      8  // Program space with no data (bss)
#define SHT_REL         9  // Relocation entries
#define SHT_SHLIB       10 // Reserved
#define SHT_DYNSYM      11 // Dynamic linker symbol table

// ELF32 Header
typedef struct {
    uint8_t  e_ident[16];   // Magic number and other info
    uint16_t e_type;        // Object file type
    uint16_t e_machine;     // Architecture
    uint32_t e_version;     // Object file version
    uint32_t e_entry;       // Entry point virtual address
    uint32_t e_phoff;       // Program header table file offset
    uint32_t e_shoff;       // Section header table file offset
    uint32_t e_flags;       // Processor-specific flags
    uint16_t e_ehsize;      // ELF header size in bytes
    uint16_t e_phentsize;   // Program header table entry size
    uint16_t e_phnum;       // Program header table entry count
    uint16_t e_shentsize;   // Section header table entry size
    uint16_t e_shnum;       // Section header table entry count
    uint16_t e_shstrndx;    // Section header string table index
} elf32_header_t;

// ELF32 Program Header
typedef struct {
    uint32_t p_type;        // Segment type
    uint32_t p_offset;      // Segment file offset
    uint32_t p_vaddr;       // Segment virtual address
    uint32_t p_paddr;       // Segment physical address (unused)
    uint32_t p_filesz;      // Segment size in file
    uint32_t p_memsz;       // Segment size in memory
    uint32_t p_flags;       // Segment flags
    uint32_t p_align;       // Segment alignment
} elf32_program_header_t;

// ELF32 Section Header
typedef struct {
    uint32_t sh_name;       // Section name (string table index)
    uint32_t sh_type;       // Section type
    uint32_t sh_flags;      // Section flags
    uint32_t sh_addr;       // Section virtual address at execution
    uint32_t sh_offset;     // Section file offset
    uint32_t sh_size;       // Section size in bytes
    uint32_t sh_link;       // Link to another section
    uint32_t sh_info;       // Additional section information
    uint32_t sh_addralign;  // Section alignment
    uint32_t sh_entsize;    // Entry size if section holds table
} elf32_section_header_t;

// ELF loading functions
int elf_validate(const void* elf_data);
int elf_load(const char* path, uint32_t* entry_point);
int elf_load_from_memory(const void* elf_data, uint32_t size, uint32_t* entry_point);

#endif // ELF_H
