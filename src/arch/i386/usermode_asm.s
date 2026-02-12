; === AOS HEADER BEGIN ===
; ./src/arch/i386/usermode_asm.s
; Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
; Licensed under CC BY-NC 4.0
; aOS Version : 0.8.8
; === AOS HEADER END ===

; Pure assembly implementation of enter_usermode
; Avoids all GCC inline asm issues with register allocation

section .text

extern set_kernel_stack

; void enter_usermode(uint32_t entry_point, uint32_t user_stack, int argc, char** argv)
; Parameters (cdecl calling convention):
;   [esp+4]  = entry_point (EIP for ring 3)
;   [esp+8]  = user_stack  (ESP for ring 3)
;   [esp+12] = argc
;   [esp+16] = argv
;
; CALLER is responsible for setting TSS.esp0 and current->kernel_stack
; BEFORE calling this function (done in userspace_run).
;
global enter_usermode
enter_usermode:

    ; Load parameters before we change the stack
    mov ebx, [esp+4]        ; ebx = entry_point
    mov ecx, [esp+8]        ; ecx = user_stack

    ; Disable interrupts for the transition
    cli

    ; Set data segment registers to user data (GDT index 4, RPL 3 = 0x23)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build iret frame: SS, ESP, EFLAGS, CS, EIP
    push dword 0x23         ; SS  (user data, RPL=3)
    push ecx                ; ESP (user stack, 16-byte aligned)
    push dword 0x202        ; EFLAGS (IF=1)
    push dword 0x1B         ; CS  (user code, RPL=3)
    push ebx                ; EIP (entry point)

    ; Ring 0 → Ring 3
    iret
