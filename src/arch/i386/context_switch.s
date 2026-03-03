global switch_context

; void switch_context(cpu_context_t* old_context, cpu_context_t* new_context)
; Save current context and restore new context
;
; Calling convention and stack layout (cdecl, i386):
;   [ebp+8]  = old_context
;   [ebp+12] = new_context
;
; cpu_context_t (i386) offsets used by this routine:
;   +0  eax, +4  ebx, +8  ecx, +12 edx,
;   +16 esi, +20 edi, +24 ebp, +28 esp,
;   +32 eip, +36 eflags, +40 cr3,
;   +44 cs, +46 ds, +48 es, +50 fs, +52 gs, +54 ss
;
; Design notes:
; - We snapshot caller state from our own stack frame first so temporary register
;   use inside this function does not corrupt the saved task context.
; - CR3 is switched before restoring most registers to ensure memory accesses
;   after the switch use the target address space.
; - Transfer to the next task is a JMP to saved EIP (not RET), because ESP/EIP
;   come from the restored context rather than this function's call frame.

switch_context:
    ; Build a small frame so we can snapshot original registers exactly
    ; before using general-purpose registers as temporary pointers.
    push ebp
    mov ebp, esp

    push eax
    push ebx
    push ecx
    push edx
    push esi
    push edi

    mov eax, [ebp+8]     ; old_context pointer
    mov edi, [ebp+12]    ; new_context pointer (kept in EDI until final jump)

    ; Save old context
    mov ebx, [ebp-4]     ; original EAX
    mov [eax+0], ebx
    mov ebx, [ebp-8]     ; original EBX
    mov [eax+4], ebx
    mov ebx, [ebp-12]    ; original ECX
    mov [eax+8], ebx
    mov ebx, [ebp-16]    ; original EDX
    mov [eax+12], ebx
    mov ebx, [ebp-20]    ; original ESI
    mov [eax+16], ebx
    mov ebx, [ebp-24]    ; original EDI
    mov [eax+20], ebx

    mov ebx, [ebp]       ; caller EBP before entering switch_context
    mov [eax+24], ebx

    lea ebx, [ebp+8]     ; resume as-if after RET (caller cleans args)
    mov [eax+28], ebx

    mov ebx, [ebp+4]     ; return address (resume EIP)
    mov [eax+32], ebx

    pushfd
    pop ebx
    mov [eax+36], ebx

    mov ebx, cr3
    mov [eax+40], ebx

    mov bx, cs
    mov [eax+44], bx
    mov bx, ds
    mov [eax+46], bx
    mov bx, es
    mov [eax+48], bx
    mov bx, fs
    mov [eax+50], bx
    mov bx, gs
    mov [eax+52], bx
    mov bx, ss
    mov [eax+54], bx

    ; Restore new context
    mov ebx, [edi+40]
    mov cr3, ebx

    mov bx, [edi+46]
    mov ds, bx
    mov bx, [edi+48]
    mov es, bx
    mov bx, [edi+50]
    mov fs, bx
    mov bx, [edi+52]
    mov gs, bx
    mov bx, [edi+54]
    mov ss, bx

    mov ebx, [edi+4]
    mov ecx, [edi+8]
    mov edx, [edi+12]
    mov esi, [edi+16]
    mov ebp, [edi+24]
    mov esp, [edi+28]

    mov eax, [edi+36]
    push eax
    popfd

    mov eax, [edi+0]
    mov edx, [edi+32]
    mov edi, [edi+20]
    jmp edx
