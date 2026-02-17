; === AOS HEADER BEGIN ===
; src/arch/x86_64/boot.s
; Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
; Licensed under CC BY-NC 4.0
; aOS Version : 0.9.0
; === AOS HEADER END ===

BITS 32

section .multiboot2
align 8
mb2_header_start:
    dd 0xE85250D6                    ; Multiboot2 magic
    dd 0                             ; Architecture (0 = i386 protected mode)
    dd mb2_header_end - mb2_header_start
    dd -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))

    ; Required end tag
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .data
align 16
gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF            ; 64-bit code segment
    dq 0x00AF92000000FFFF            ; data segment
    dq 0x00AFFA000000FFFF            ; user code segment (ring 3)
    dq 0x00AFF2000000FFFF            ; user data segment (ring 3)
gdt64_tss_low:
    dq 0x0000000000000000            ; populated at runtime
gdt64_tss_high:
    dq 0x0000000000000000            ; populated at runtime
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64
gdt64_end:

section .bss
alignb 16
stack_bottom:
    resb 16384
stack_top:

alignb 16
tss64:
    resd 1                           ; reserved
global tss_rsp0
tss_rsp0:
    resq 1                           ; RSP0
    resq 1                           ; RSP1
    resq 1                           ; RSP2
    resq 1                           ; reserved
    resq 1                           ; IST1
    resq 1                           ; IST2
    resq 1                           ; IST3
    resq 1                           ; IST4
    resq 1                           ; IST5
    resq 1                           ; IST6
    resq 1                           ; IST7
    resq 1                           ; reserved
    resw 1                           ; reserved
    resw 1                           ; I/O map base
tss64_end:

alignb 4
mb_magic_saved:
    resd 1
mb_info_saved:
    resd 1

alignb 4096
pml4_table:
    resq 512
pdpt_table:
    resq 512
pd_tables:
    resq 2048                        ; 4 * 1GiB = 4GiB identity map

section .text
global _start
extern kernel_main

_start:
    cli

    ; Temporary stack in 32-bit mode
    mov esp, stack_top

    ; Preserve GRUB multiboot magic and info pointer across mode switch setup.
    mov [mb_magic_saved], eax
    mov [mb_info_saved], ebx

    ; Zero page tables: PML4 + PDPT + 4xPD = 6 tables = 24576 bytes
    mov edi, pml4_table
    xor eax, eax
    mov ecx, 6144
    rep stosd

    ; PML4[0] -> PDPT
    mov eax, pdpt_table
    or eax, 0x3
    mov dword [pml4_table + 0], eax
    mov dword [pml4_table + 4], 0

    ; PDPT[0..3] -> PD tables
    mov eax, pd_tables
    or eax, 0x3
    mov dword [pdpt_table + 0], eax
    mov dword [pdpt_table + 4], 0

    mov eax, pd_tables + 0x1000
    or eax, 0x3
    mov dword [pdpt_table + 8], eax
    mov dword [pdpt_table + 12], 0

    mov eax, pd_tables + 0x2000
    or eax, 0x3
    mov dword [pdpt_table + 16], eax
    mov dword [pdpt_table + 20], 0

    mov eax, pd_tables + 0x3000
    or eax, 0x3
    mov dword [pdpt_table + 24], eax
    mov dword [pdpt_table + 28], 0

    ; PD entries: identity-map first 4 GiB using 2 MiB pages
    xor ecx, ecx
    xor ebx, ebx
.map_pd:
    mov eax, ebx
    or eax, 0x83                      ; present|write|PS (supervisor by default)
    mov dword [pd_tables + ecx * 8], eax
    mov dword [pd_tables + ecx * 8 + 4], 0
    add ebx, 0x200000
    inc ecx
    cmp ecx, (512 * 4)
    jne .map_pd

    ; Enable PAE
    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    ; Load PML4 into CR3
    mov eax, pml4_table
    mov cr3, eax

    ; Enable long mode in EFER
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)                  ; LME
    wrmsr

    ; Enable paging
    mov eax, cr0
    or eax, 0x80000000                ; PG
    mov cr0, eax

    ; Enable SSE/SSE2 so compiler-emitted XMM instructions are valid
    mov eax, cr0
    and eax, 0xFFFFFFFB               ; clear EM
    or eax, (1 << 1) | (1 << 5)       ; set MP|NE
    mov cr0, eax

    mov eax, cr4
    or eax, (1 << 9) | (1 << 10)      ; OSFXSR|OSXMMEXCPT
    mov cr4, eax

    ; Load 64-bit GDT and far jump
    lgdt [gdt64_ptr]
    jmp 0x08:long_mode_entry

BITS 64
long_mode_entry:
    ; Build and publish TSS descriptor in the runtime GDT.
    lea rcx, [rel tss64]
    mov rax, (tss64_end - tss64 - 1) ; TSS limit

    mov rbx, rax
    and rbx, 0xFFFF

    mov rdx, rcx
    and rdx, 0xFFFFFF
    shl rdx, 16
    or rbx, rdx

    mov rdx, 0x89                    ; present + available 64-bit TSS
    shl rdx, 40
    or rbx, rdx

    mov rdx, rax
    shr rdx, 16
    and rdx, 0xF
    shl rdx, 48
    or rbx, rdx

    mov rdx, rcx
    shr rdx, 24
    and rdx, 0xFF
    shl rdx, 56
    or rbx, rdx

    mov [rel gdt64_tss_low], rbx

    mov rdx, rcx
    shr rdx, 32
    mov [rel gdt64_tss_high], rdx

    ; Default kernel ring-0 stack for privilege transitions.
    lea rax, [rel stack_top]
    mov [rel tss_rsp0], rax
    mov word [rel tss64 + 102], (tss64_end - tss64)

    mov ax, 0x28                     ; TSS selector (GDT entry 5)
    ltr ax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor rbp, rbp

    ; Pass the original GRUB values to kernel_main.
    mov edi, dword [rel mb_magic_saved]
    mov esi, dword [rel mb_info_saved]

    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
