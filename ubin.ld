/* Userspace binary linker script — loads at traditional ELF base 0x08048000 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {
    . = 0x08048000;

    .text ALIGN(16) : {
        *(.text.entry)
        *(.text*)
    }

    .rodata ALIGN(16) : {
        *(.rodata*)
    }

    .data ALIGN(16) : {
        *(.data*)
    }

    .bss ALIGN(16) : {
        *(.bss*)
        *(COMMON)
    }

    _end = .;
}
