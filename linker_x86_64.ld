ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 1M;
    __kernel_start = .;

    .multiboot2 ALIGN(8) : AT(ADDR(.multiboot2)) {
        KEEP(*(.multiboot2))
    } :text

    .text ALIGN(4096) : {
        *(.text*)
    } :text

    .rodata ALIGN(4096) : {
        *(.rodata*)
    } :rodata

    .data ALIGN(4096) : {
        *(.data*)
    } :data

    .bss ALIGN(4096) : {
        *(.bss*)
        *(COMMON)
    } :data

    __kernel_end = .;
}
