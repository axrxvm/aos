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
; cpu_context_t layout (32-bit legacy fields):
;   +0 eax, +4 ebx, +8 ecx, +12 edx, +16 esi, +20 edi,
;   +24 ebp, +28 esp, +32 eip, +36 eflags, +40 cr3

switch_context:
    mov r8, rdi
    mov r9, rsi

    ; Save old context (low 32-bit values; kernel currently uses low memory addresses)
    mov [r8 + 0], eax
    mov [r8 + 4], ebx
    mov [r8 + 8], ecx
    mov [r8 + 12], edx
    mov [r8 + 16], esi
    mov [r8 + 20], edi
    mov [r8 + 24], ebp
    mov [r8 + 28], esp

    mov eax, [rsp]           ; return RIP for resumed task
    mov [r8 + 32], eax

    pushfq
    pop rax
    mov [r8 + 36], eax

    mov rax, cr3
    mov [r8 + 40], eax

    ; Restore CR3 first
    mov eax, [r9 + 40]
    mov cr3, rax

    ; Restore general state
    mov ebx, [r9 + 4]
    mov ecx, [r9 + 8]
    mov edx, [r9 + 12]
    mov esi, [r9 + 16]
    mov edi, [r9 + 20]
    mov ebp, [r9 + 24]
    mov esp, [r9 + 28]

    mov eax, [r9 + 36]
    push rax
    popfq

    mov eax, [r9 + 0]
    mov edx, [r9 + 32]
    jmp rdx
