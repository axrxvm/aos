ENTRY(_start)

SECTIONS {
    . = 1M;

    /* Force multiboot header into its own loadable section first */
    .multiboot ALIGN(4) : AT(ADDR(.multiboot)) {
        KEEP(*(.multiboot))
    }

    .text ALIGN(4) : {
        *(.text*)
    }

    .rodata ALIGN(4) : { *(.rodata*) }
    .data ALIGN(4)   : { *(.data*) }

    .bss ALIGN(4) : {
        *(.bss*)
        *(COMMON)
    }
}
