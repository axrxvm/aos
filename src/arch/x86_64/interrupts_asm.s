; === AOS HEADER BEGIN ===
; src/arch/x86_64/interrupts_asm.s
; Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
; Licensed under CC BY-NC 4.0
; aOS Version : 0.9.0
; === AOS HEADER END ===

BITS 64

extern isr_handler_common
extern irq_handler_common

global idt_load

%macro PUSH_GPRS_LEGACY 0
    push rax
    push rcx
    push rdx
    push rbx
    push rsp
    push rbp
    push rsi
    push rdi
%endmacro

%macro POP_GPRS_LEGACY 0
    pop rdi
    pop rsi
    pop rbp
    add rsp, 8          ; discard saved rsp slot (esp_dummy)
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

%macro PUSH_EXTRA_GPRS 0
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_EXTRA_GPRS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
%endmacro

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli
    push qword 0        ; useresp placeholder
    push qword 0        ; ss placeholder
    push qword 0        ; err_code placeholder
    push qword %1       ; int_no
    jmp isr_stub_common
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    pop r11             ; CPU-pushed real error code
    push qword 0        ; useresp placeholder
    push qword 0        ; ss placeholder
    push r11            ; err_code
    push qword %1       ; int_no
    jmp isr_stub_common
%endmacro

%macro ISR_IRQ_NOERRCODE 1
global isr%1
isr%1:
    cli
    push qword 0        ; useresp placeholder
    push qword 0        ; ss placeholder
    push qword 0        ; err_code placeholder
    push qword %1       ; int_no
    jmp irq_stub_common
%endmacro

isr_stub_common:
    PUSH_GPRS_LEGACY
    push qword 0        ; ds placeholder
    PUSH_EXTRA_GPRS

    lea rdi, [rsp + 64] ; registers_t starts after saved r8-r15
    call isr_handler_common

    POP_EXTRA_GPRS
    add rsp, 8          ; discard ds placeholder
    POP_GPRS_LEGACY
    add rsp, 32         ; int_no + err_code + ss + useresp
    iretq

irq_stub_common:
    PUSH_GPRS_LEGACY
    push qword 0
    PUSH_EXTRA_GPRS

    lea rdi, [rsp + 64]
    call irq_handler_common

    POP_EXTRA_GPRS
    add rsp, 8
    POP_GPRS_LEGACY
    add rsp, 32
    iretq

; CPU exceptions 0-31
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_ERRCODE   21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_ERRCODE   29
ISR_ERRCODE   30
ISR_NOERRCODE 31

; PIC IRQs 32-47
ISR_IRQ_NOERRCODE 32
ISR_IRQ_NOERRCODE 33
ISR_IRQ_NOERRCODE 34
ISR_IRQ_NOERRCODE 35
ISR_IRQ_NOERRCODE 36
ISR_IRQ_NOERRCODE 37
ISR_IRQ_NOERRCODE 38
ISR_IRQ_NOERRCODE 39
ISR_IRQ_NOERRCODE 40
ISR_IRQ_NOERRCODE 41
ISR_IRQ_NOERRCODE 42
ISR_IRQ_NOERRCODE 43
ISR_IRQ_NOERRCODE 44
ISR_IRQ_NOERRCODE 45
ISR_IRQ_NOERRCODE 46
ISR_IRQ_NOERRCODE 47

; INT 0x80 syscall vector
global isr128
isr128:
    cli
    push qword 0
    push qword 0
    push qword 0
    push qword 128
    jmp isr_stub_common

idt_load:
    lidt [rdi]
    ret
