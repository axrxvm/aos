global gdt_load
global tss_load
; extern gdt_ptr ; Not actually needed here as address is passed

gdt_load:
    mov eax, [esp+4]  ; Get the address of the GDT pointer from stack
    lgdt [eax]        ; Load the GDT pointer

    ; Reload segment registers to use the new GDT
    ; 0x08 is the offset of the Kernel Code segment (index 1 * 8 bytes)
    ; 0x10 is the offset of the Kernel Data segment (index 2 * 8 bytes)
    mov ax, 0x10      ; Load Kernel Data Segment selector into AX
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush   ; Far jump to Kernel Code Segment to reload CS
.flush:
    ret

tss_load:
    mov ax, [esp+4]   ; Get TSS segment selector (passed as 16-bit)
    ltr ax            ; Load Task Register
    ret

