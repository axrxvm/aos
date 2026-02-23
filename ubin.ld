/* Userspace binary linker script — loads at traditional ELF base 0x08048000 */
OUTPUT_FORMAT(elf32-i386)
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
