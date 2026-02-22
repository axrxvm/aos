; === AOS HEADER BEGIN ===
; bootloader/abl_mbr.asm
; Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
; Licensed under CC BY-NC 4.0
; aOS Version : 0.9.2
; === AOS HEADER END ===

; ABL (aOS Bootloader)
; ABL is the minimal, aOS-native disk boot path.
; Goals:
; - tiny stage1 (MBR) with no external dependencies
; - deterministic handoff tailored for aOS boot flow
; - simple layout controlled by the `install` command
;
; This file is ABL stage1 (MBR):
; - runs in 16-bit real mode at 0x7C00
; - reads stage2 from disk using INT 13h extensions (LBA)
; - transfers control to stage2 at 0x8000

BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Patch the DAP fields from installer-populated config values.
    mov ax, [cfg_stage2_sectors]
    mov [dap_sector_count], ax
    mov eax, [cfg_stage2_lba]
    mov [dap_lba_low], eax

    ; Read stage2 loader to 0000:8000.
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc boot_fail

    mov dl, [boot_drive]
    jmp 0x0000:0x8000

boot_fail:
    cli
.hang:
    hlt
    jmp .hang

cfg_block:
    db 'ABLMBR1!'
cfg_stage2_lba:
    dd 0
cfg_stage2_sectors:
    dw 0

boot_drive:
    db 0

dap:
    db 0x10
    db 0x00
dap_sector_count:
    dw 0
    dw 0x8000
    dw 0x0000
dap_lba_low:
    dd 0
    dd 0

times 446 - ($ - $$) db 0
times 64 db 0
dw 0xAA55
