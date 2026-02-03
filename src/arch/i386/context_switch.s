global switch_context

; void switch_context(cpu_context_t* old_context, cpu_context_t* new_context)
; Save current context and restore new context

switch_context:
    mov eax, [esp+4]    ; old_context pointer
    mov edx, [esp+8]    ; new_context pointer

    ; Save old context
    ; Save general purpose registers
    mov [eax+0], ebx    ; eax will be saved last
    mov [eax+4], ebx
    mov [eax+8], ecx
    mov [eax+12], edx
    mov [eax+16], esi
    mov [eax+20], edi
    mov [eax+24], ebp
    mov [eax+28], esp
    
    ; Save EIP (return address)
    mov ebx, [esp]
    mov [eax+32], ebx
    
    ; Save EFLAGS
    pushfd
    pop ebx
    mov [eax+36], ebx
    
    ; Save CR3
    mov ebx, cr3
    mov [eax+40], ebx
    
    ; Save segment registers
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
    ; Restore CR3 (page directory)
    mov ebx, [edx+40]
    mov cr3, ebx
    
    ; Restore segment registers
    mov bx, [edx+46]
    mov ds, bx
    mov bx, [edx+48]
    mov es, bx
    mov bx, [edx+50]
    mov fs, bx
    mov bx, [edx+52]
    mov gs, bx
    mov bx, [edx+54]
    mov ss, bx
    
    ; Restore general purpose registers
    mov ebx, [edx+4]
    mov ecx, [edx+8]
    mov esi, [edx+16]
    mov edi, [edx+20]
    mov ebp, [edx+24]
    mov esp, [edx+28]
    
    ; Restore EFLAGS
    mov eax, [edx+36]
    push eax
    popfd
    
    ; Restore EAX
    mov eax, [edx+0]
    
    ; Jump to new EIP
    mov edx, [edx+32]
    jmp edx
