; boot.s or kernel entry .s file

; Multiboot Header (must be within the first 8 KB of the file)
section .multiboot align=4 progbits alloc
align 4
global multiboot_header:
    dd 0x1BADB002          ; magic number
    dd 0x0                 ; flags
    dd -(0x1BADB002 + 0x0) ; checksum

section .text
global _start
extern kernel_main
_start:
    mov eax, 0x1BADB002 ; this is actually redundant—GRUB already sets it
    mov ebx, ebx        ; GRUB already sets EBX to multiboot_info pointer

    push ebx            ; arg2
    push eax            ; arg1
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
