; boot.s or kernel entry .s file

; Multiboot Header (must be within the first 8 KB of the file)
section .multiboot align=4 progbits alloc
align 4
global multiboot_header
multiboot_header:
    dd 0x1BADB002          ; magic number
    dd 0x0                 ; flags
    dd -(0x1BADB002 + 0x0) ; checksum

; Multiboot2 header (must be within the first 32 KB of the file)
section .multiboot2 align=8 progbits alloc
align 8
global multiboot2_header
multiboot2_header:
    dd 0xE85250D6                       ; Multiboot2 magic
    dd 0                                ; Architecture: i386
    dd multiboot2_header_end - multiboot2_header
    dd -(0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header))
    dw 0                                ; End tag type
    dw 0                                ; End tag flags
    dd 8                                ; End tag size
multiboot2_header_end:

section .text
global _start
extern kernel_main
_start:
    push ebx            ; arg2
    push eax            ; arg1
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
