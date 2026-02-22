; === AOS HEADER BEGIN ===
; bootloader/abl_stage2.asm
; Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
; Licensed under CC BY-NC 4.0
; aOS Version : 0.9.2
; === AOS HEADER END ===

; ABL (aOS Bootloader)
; ABL is the minimal, fully aOS-custom bootloader used for disk boot.
; Goals:
; - keep the boot chain small and predictable
; - pass exactly the boot metadata aOS needs
; - avoid relying on host-side bootloader installation steps
;
; This file is ABL stage2:
; - enters protected mode
; - reads kernel payload from fixed LBA regions
; - loads ELF PT_LOAD segments into memory
; - captures BIOS memory information (legacy + E820 map)
; - jumps to kernel entry with ABL boot magic + info pointer

BITS 16
ORG 0x8000

%define ABL_BOOT_MAGIC 0x314C4241
%define ABL_INFO_FLAG_BOOT_DRIVE     0x00000001
%define ABL_INFO_FLAG_KERNEL_PAYLOAD 0x00000002
%define ABL_INFO_FLAG_MEMORY_INFO    0x00000004
%define ABL_INFO_FLAG_MEMORY_MAP     0x00000008
%define ABL_INFO_FLAG_STAGE2_LAYOUT  0x00000010
%define ABL_INFO_FLAG_VBE_INFO       0x00000080
%define ABL_INFO_FLAG_FRAMEBUFFER    0x00000100
%define ABL_E820_MAX_ENTRIES         48
%define ABL_MMAP_ENTRY_SIZE          24
%define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
%define MULTIBOOT_FRAMEBUFFER_TYPE_RGB 1

%define VBE_MODEINFO_PITCH       16
%define VBE_MODEINFO_WIDTH       18
%define VBE_MODEINFO_HEIGHT      20
%define VBE_MODEINFO_BPP         25
%define VBE_MODEINFO_FRAMEBUFFER 40

start16:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    call detect_legacy_memory
    xor ax, ax
    mov ds, ax
    mov es, ax
    call detect_memory_map
    xor ax, ax
    mov ds, ax
    mov es, ax
    call detect_vbe_info
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; Fast A20 gate
    in al, 0x92
    or al, 0x02
    out 0x92, al

    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:start32

detect_legacy_memory:
    mov dword [bios_mem_upper_kb], 0

    xor eax, eax
    int 0x12
    movzx eax, ax
    mov [bios_mem_lower_kb], eax

    xor eax, eax
    mov ah, 0x88
    int 0x15
    jc .try_e801
    and eax, 0x0000FFFF
    mov [bios_mem_upper_kb], eax

.try_e801:
    mov ax, 0xE801
    int 0x15
    jc .done

    movzx eax, ax
    movzx ebx, bx
    or eax, ebx
    jnz .e801_select_done

    movzx eax, cx
    movzx ebx, dx

.e801_select_done:
    test eax, eax
    jnz .e801_calc
    test ebx, ebx
    jz .done

.e801_calc:
    shl ebx, 6
    add eax, ebx
    cmp eax, [bios_mem_upper_kb]
    jbe .done
    mov [bios_mem_upper_kb], eax
.done:
    ret

detect_memory_map:
    mov word [e820_entry_count], 0
    mov word [e820_write_ptr], e820_entries
    xor ebx, ebx

.query:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 20
    mov di, e820_scratch
    int 0x15
    jc .done
    xor ax, ax
    mov ds, ax
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .check_more

    mov ax, [e820_entry_count]
    cmp ax, ABL_E820_MAX_ENTRIES
    jae .check_more

    mov si, [e820_write_ptr]
    mov dword [si + 0], 20
    mov eax, [e820_scratch + 0]
    mov [si + 4], eax
    mov eax, [e820_scratch + 4]
    mov [si + 8], eax
    mov eax, [e820_scratch + 8]
    mov [si + 12], eax
    mov eax, [e820_scratch + 12]
    mov [si + 16], eax
    mov eax, [e820_scratch + 16]
    mov [si + 20], eax
    add si, ABL_MMAP_ENTRY_SIZE
    mov [e820_write_ptr], si
    inc word [e820_entry_count]

.check_more:
    test ebx, ebx
    jnz .query
.done:
    ret

detect_vbe_info:
    mov byte [vbe_available], 0

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov dword [vbe_controller_info], 0x32454256 ; "VBE2"

    mov ax, 0x4F00
    mov di, vbe_controller_info
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov ax, 0x4F03
    int 0x10
    cmp ax, 0x004F
    jne .done
    mov [vbe_current_mode], bx

    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, [vbe_current_mode]
    and cx, 0x3FFF
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    jne .done

    mov byte [vbe_available], 1
.done:
    ret

BITS 32
start32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x0009F000
    cld

    cmp dword [cfg_magic], 0x32474643 ; "CFG2"
    jne boot_fail

    mov eax, [cfg_kernel_lba]
    mov ecx, [cfg_kernel_sectors]
    mov edi, [cfg_kernel_buffer]
    test ecx, ecx
    jz boot_fail

.read_kernel_loop:
    mov ebx, edi
    call ata_read_sector_lba28
    jc boot_fail
    add eax, 1
    add edi, 512
    dec ecx
    jnz .read_kernel_loop

    mov ebp, cfg_segments
    mov ebx, [cfg_segment_count]

.load_segment_loop:
    test ebx, ebx
    jz .jump_kernel

    mov edi, [ebp + 0]      ; destination physical address
    mov esi, [ebp + 4]      ; file offset inside kernel blob
    add esi, [cfg_kernel_buffer]
    mov edx, [ebp + 8]      ; filesz
    mov eax, [ebp + 12]     ; memsz

    mov ecx, edx
    rep movsb

    sub eax, edx
    jbe .next_segment

    mov ecx, eax
    xor eax, eax
    rep stosb

.next_segment:
    add ebp, 16
    dec ebx
    jmp .load_segment_loop

.jump_kernel:
    mov edi, abl_info
    xor eax, eax
    mov ecx, (abl_info_end - abl_info) / 4
    rep stosd

    mov dword [abl_info_magic], ABL_BOOT_MAGIC
    mov dword [abl_info_version], 1
    mov dword [abl_info_flags], (ABL_INFO_FLAG_BOOT_DRIVE | ABL_INFO_FLAG_KERNEL_PAYLOAD | ABL_INFO_FLAG_STAGE2_LAYOUT)

    movzx eax, byte [boot_drive]
    mov [abl_info_boot_drive], eax
    mov eax, [cfg_stage2_lba]
    mov [abl_info_stage2_lba], eax
    mov eax, [cfg_stage2_sectors]
    mov [abl_info_stage2_sectors], eax

    mov eax, [cfg_kernel_lba]
    mov [abl_info_kernel_lba], eax
    mov eax, [cfg_kernel_sectors]
    mov [abl_info_kernel_sectors], eax

    mov eax, [bios_mem_lower_kb]
    mov [abl_info_mem_lower_kb], eax
    mov eax, [bios_mem_upper_kb]
    mov [abl_info_mem_upper_kb], eax
    mov eax, [abl_info_mem_lower_kb]
    or eax, [abl_info_mem_upper_kb]
    jz .skip_mem_info
    or dword [abl_info_flags], ABL_INFO_FLAG_MEMORY_INFO
.skip_mem_info:

    xor eax, eax
    mov ax, [e820_entry_count]
    mov [abl_info_mmap_entry_count], eax
    test eax, eax
    jz .skip_mmap
    mov dword [abl_info_mmap_addr], e820_entries
    mov dword [abl_info_mmap_entry_size], ABL_MMAP_ENTRY_SIZE
    mov ecx, ABL_MMAP_ENTRY_SIZE
    mul ecx
    mov [abl_info_mmap_length], eax
    or dword [abl_info_flags], ABL_INFO_FLAG_MEMORY_MAP
.skip_mmap:

    cmp byte [vbe_available], 0
    je .skip_vbe

    mov dword [abl_info_vbe_control_info_addr], vbe_controller_info
    mov dword [abl_info_vbe_mode_info_addr], vbe_mode_info
    xor eax, eax
    mov ax, [vbe_current_mode]
    mov [abl_info_vbe_mode], eax
    or dword [abl_info_flags], ABL_INFO_FLAG_VBE_INFO

    xor eax, eax
    mov ax, [vbe_mode_info + VBE_MODEINFO_PITCH]
    mov [abl_info_framebuffer_pitch], eax
    xor eax, eax
    mov ax, [vbe_mode_info + VBE_MODEINFO_WIDTH]
    mov [abl_info_framebuffer_width], eax
    xor eax, eax
    mov ax, [vbe_mode_info + VBE_MODEINFO_HEIGHT]
    mov [abl_info_framebuffer_height], eax
    xor eax, eax
    mov al, [vbe_mode_info + VBE_MODEINFO_BPP]
    mov [abl_info_framebuffer_bpp], eax
    mov eax, [vbe_mode_info + VBE_MODEINFO_FRAMEBUFFER]
    mov [abl_info_framebuffer_addr], eax

    mov eax, MULTIBOOT_FRAMEBUFFER_TYPE_RGB
    cmp byte [vbe_mode_info + VBE_MODEINFO_BPP], 8
    ja .fb_type_done
    mov eax, MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED
.fb_type_done:
    mov [abl_info_framebuffer_type], eax

    mov eax, [abl_info_framebuffer_addr]
    test eax, eax
    jz .skip_fb
    mov eax, [abl_info_framebuffer_width]
    test eax, eax
    jz .skip_fb
    mov eax, [abl_info_framebuffer_height]
    test eax, eax
    jz .skip_fb
    mov eax, [abl_info_framebuffer_bpp]
    test eax, eax
    jz .skip_fb
    or dword [abl_info_flags], ABL_INFO_FLAG_FRAMEBUFFER
.skip_fb:
.skip_vbe:

    mov eax, ABL_BOOT_MAGIC
    mov ebx, abl_info
    mov edx, [cfg_entry]
    jmp edx

boot_fail:
    cli
.hang:
    hlt
    jmp .hang

; Read one sector from ATA primary bus using 28-bit LBA PIO.
; IN: EAX=lba, EBX=destination
; OUT: CF clear on success, set on error
ata_read_sector_lba28:
    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov esi, eax

.wait_not_busy:
    mov dx, 0x1F7
    in al, dx
    test al, 0x80
    jnz .wait_not_busy

    mov dx, 0x1F2
    mov al, 1
    out dx, al

    mov eax, esi
    mov dx, 0x1F3
    out dx, al
    shr eax, 8
    mov dx, 0x1F4
    out dx, al
    shr eax, 8
    mov dx, 0x1F5
    out dx, al
    shr eax, 8
    and al, 0x0F
    or al, 0xE0
    mov dx, 0x1F6
    out dx, al

    mov dx, 0x1F7
    mov al, 0x20
    out dx, al

.wait_drq:
    in al, dx
    test al, 0x01
    jnz .io_error
    test al, 0x80
    jnz .wait_drq
    test al, 0x08
    jz .wait_drq

    mov dx, 0x1F0
    mov edi, ebx
    mov ecx, 256
    rep insw

    clc
    jmp .done

.io_error:
    stc

.done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

ALIGN 8
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

boot_drive:
    db 0

bios_mem_lower_kb:
    dd 0
bios_mem_upper_kb:
    dd 0
e820_entry_count:
    dw 0
e820_write_ptr:
    dw 0
e820_scratch:
    times ABL_MMAP_ENTRY_SIZE db 0
e820_entries:
    times (ABL_E820_MAX_ENTRIES * ABL_MMAP_ENTRY_SIZE) db 0
vbe_available:
    db 0
vbe_current_mode:
    dw 0
ALIGN 16
vbe_controller_info:
    times 512 db 0
ALIGN 16
vbe_mode_info:
    times 256 db 0

ALIGN 16
cfg_block:
    db 'ABLCFG2!'
cfg_magic:
    dd 0
cfg_stage2_lba:
    dd 0
cfg_stage2_sectors:
    dd 0
cfg_kernel_lba:
    dd 0
cfg_kernel_sectors:
    dd 0
cfg_entry:
    dd 0
cfg_kernel_buffer:
    dd 0x00800000
cfg_segment_count:
    dd 0
cfg_segments:
    times 8 dd 0, 0, 0, 0

ALIGN 16
abl_info:
abl_info_magic:
    dd 0
abl_info_version:
    dd 0
abl_info_flags:
    dd 0
abl_info_boot_drive:
    dd 0
abl_info_stage2_lba:
    dd 0
abl_info_stage2_sectors:
    dd 0
abl_info_kernel_lba:
    dd 0
abl_info_kernel_sectors:
    dd 0
abl_info_mem_lower_kb:
    dd 0
abl_info_mem_upper_kb:
    dd 0
abl_info_mmap_addr:
    dd 0
abl_info_mmap_length:
    dd 0
abl_info_mmap_entry_size:
    dd 0
abl_info_mmap_entry_count:
    dd 0
abl_info_cmdline_addr:
    dd 0
abl_info_modules_addr:
    dd 0
abl_info_modules_count:
    dd 0
abl_info_vbe_control_info_addr:
    dd 0
abl_info_vbe_mode_info_addr:
    dd 0
abl_info_vbe_mode:
    dd 0
abl_info_vbe_interface_seg:
    dd 0
abl_info_vbe_interface_off:
    dd 0
abl_info_vbe_interface_len:
    dd 0
abl_info_framebuffer_addr:
    dd 0
abl_info_framebuffer_pitch:
    dd 0
abl_info_framebuffer_width:
    dd 0
abl_info_framebuffer_height:
    dd 0
abl_info_framebuffer_bpp:
    dd 0
abl_info_framebuffer_type:
    dd 0
abl_info_end:
