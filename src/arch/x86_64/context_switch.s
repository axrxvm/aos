; === AOS HEADER BEGIN ===
; src/arch/x86_64/context_switch.s
; Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
; Licensed under CC BY-NC 4.0
; aOS Version : 0.9.0
; === AOS HEADER END ===

BITS 64

global switch_context

; void switch_context(cpu_context_t* old_context, cpu_context_t* new_context)
; SysV ABI:
;   rdi = old_context
;   rsi = new_context
;
; cpu_context_t layout on x86_64 (legacy names, 64-bit values):
;   +0 eax, +8 ebx, +16 ecx, +24 edx, +32 esi, +40 edi,
;   +48 ebp, +56 esp, +64 eip, +72 eflags, +80 cr3

switch_context:
    mov r8, rdi
    mov r9, rsi

    ; Save old context
    mov [r8 + 0], rax
    mov [r8 + 8], rbx
    mov [r8 + 16], rcx
    mov [r8 + 24], rdx
    mov [r8 + 32], rsi
    mov [r8 + 40], rdi
    mov [r8 + 48], rbp
    mov [r8 + 56], rsp

    mov rax, [rsp]           ; return RIP for resumed task
    mov [r8 + 64], rax

    pushfq
    pop rax
    mov [r8 + 72], rax

    mov rax, cr3
    mov [r8 + 80], rax

    ; Restore CR3 first
    mov rax, [r9 + 80]
    mov cr3, rax

    ; Restore general state
    mov rbx, [r9 + 8]
    mov rcx, [r9 + 16]
    mov rdx, [r9 + 24]
    mov rsi, [r9 + 32]
    mov rdi, [r9 + 40]
    mov rbp, [r9 + 48]
    mov rsp, [r9 + 56]

    mov rax, [r9 + 72]
    push rax
    popfq

    mov rax, [r9 + 0]
    mov rdx, [r9 + 64]
    jmp rdx
