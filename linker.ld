ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);     /* Read + Execute */
    rodata PT_LOAD FLAGS(4);   /* Read only */
    data PT_LOAD FLAGS(6);     /* Read + Write */
}

SECTIONS {
    . = 1M;
    __kernel_start = .;

    /* Force multiboot header into its own loadable section first */
    .multiboot ALIGN(4) : AT(ADDR(.multiboot)) {
        KEEP(*(.multiboot))
    } :text

    .text ALIGN(4096) : {
        *(.text*)
    } :text

    .rodata ALIGN(4096) : { *(.rodata*) } :rodata
    .data ALIGN(4096)   : { *(.data*) } :data

    .bss ALIGN(4096) : {
        *(.bss*)
        *(COMMON)
    } :data

    __kernel_end = .;
}
