; === AOS HEADER BEGIN ===
; src/arch/x86_64/usermode_asm.s
; Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
; Licensed under CC BY-NC 4.0
; aOS Version : 0.9.0
; === AOS HEADER END ===

BITS 64
section .text

; void enter_usermode(uint32_t entry_point, uint32_t user_stack, int argc, char** argv)
; System V AMD64 calling convention:
;   rdi = entry_point
;   rsi = user_stack
;   rdx = argc (unused)
;   rcx = argv (unused)
global enter_usermode
enter_usermode:
    mov rbx, rdi                    ; target RIP in ring 3
    mov rcx, rsi                    ; target RSP in ring 3

    cli

    ; Use user data selectors before transitioning.
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build iretq frame: SS, RSP, RFLAGS, CS, RIP
    push qword 0x23                 ; SS
    push rcx                        ; RSP
    push qword 0x202                ; RFLAGS (IF=1)
    push qword 0x1B                 ; CS
    push rbx                        ; RIP

    iretq

.hang:
    cli
    hlt
    jmp .hang
