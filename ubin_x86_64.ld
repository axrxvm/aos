/* Userspace binary linker script (x86_64) */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5); /* R+X */
    data PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS {
    . = 0x08048000;

    .text ALIGN(16) : {
        *(.text.entry)
        *(.text*)
    } :text

    .rodata ALIGN(16) : {
        *(.rodata*)
    } :text

    .data ALIGN(16) : {
        *(.data*)
    } :data

    .bss ALIGN(16) : {
        *(.bss*)
        *(COMMON)
    } :data

    _end = .;
}
