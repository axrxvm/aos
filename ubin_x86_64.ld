/* Userspace binary linker script (x86_64) */
OUTPUT_FORMAT(elf64-x86-64)
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
