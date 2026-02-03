; Assembly functions for paging support
global enable_paging_asm
global load_page_directory
global get_cr3

; Enable paging by setting the PG bit in CR0
; Parameter: page directory physical address (on stack)
enable_paging_asm:
    push ebp
    mov ebp, esp
    
    ; Load page directory address into CR3
    mov eax, [ebp + 8]  ; Get page directory address from stack
    mov cr3, eax
    
    ; Enable paging by setting PG bit (bit 31) in CR0
    mov eax, cr0
    or eax, 0x80000000  ; Set PG bit
    mov cr0, eax
    
    pop ebp
    ret

; Load page directory into CR3
; Parameter: page directory physical address (on stack)
load_page_directory:
    push ebp
    mov ebp, esp
    
    mov eax, [ebp + 8]  ; Get page directory address from stack
    mov cr3, eax
    
    pop ebp
    ret

; Get current page directory address from CR3
get_cr3:
    mov eax, cr3
    ret