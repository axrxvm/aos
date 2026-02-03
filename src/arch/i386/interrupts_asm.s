extern isr_handler_common  ; Defined in C

; Macro for ISRs that don't push an error code
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli             ; Disable interrupts
    push byte 0     ; Push a dummy error code
    push byte %1    ; Push the interrupt number
    jmp isr_stub_common
%endmacro

; Macro for ISRs that push an error code
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli             ; Disable interrupts
    ; Error code is already on stack
    push byte %1    ; Push the interrupt number
    jmp isr_stub_common
%endmacro

isr_stub_common:
    pusha           ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax

    mov ax, ds      ; Lower 16 bits of ds register contains segment selector
    push eax        ; Save the data segment selector (pushed as 32-bit value)

    mov ax, 0x10    ; Load Kernel Data Segment selector (0x10, from GDT)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; SS is already correct due to interrupt/exception stack switch if applicable

    cld             ; Clear direction flag for string operations, good practice

    ; Pass pointer to stack (where registers_t structure is formed) to C handler
    push esp        ; Push pointer to the registers structure
    call isr_handler_common
    add esp, 4      ; Clean up pushed pointer

    pop eax         ; Restore original data segment selector (eax has the pushed ds)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa            ; Pops edi,esi,ebp,esp,ebx,edx,ecx,eax
    add esp, 8      ; Clean up pushed error code and interrupt number
    ; For faults that should return to the faulting instruction, 'sti' might be problematic here.
    ; It's generally re-enabled by iret if EFLAGS had interrupts enabled.
    ; However, for safety, especially during development:
    ; sti          ; Re-enable interrupts (conditionally, or remove if iret handles it)
    iret            ; Return from interrupt

; Define ISRs for exceptions 0-31
ISR_NOERRCODE 0   ; Division by Zero
ISR_NOERRCODE 1   ; Debug
ISR_NOERRCODE 2   ; Non Maskable Interrupt
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Into Detected Overflow
ISR_NOERRCODE 5   ; Out of Bounds
ISR_NOERRCODE 6   ; Invalid Opcode
ISR_NOERRCODE 7   ; No Coprocessor / Device Not Available
ISR_ERRCODE   8   ; Double Fault
ISR_NOERRCODE 9   ; Coprocessor Segment Overrun (obsolete)
ISR_ERRCODE   10  ; Invalid TSS
ISR_ERRCODE   11  ; Segment Not Present
ISR_ERRCODE   12  ; Stack Fault
ISR_ERRCODE   13  ; General Protection Fault
ISR_ERRCODE   14  ; Page Fault
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; Floating Point Exception / x87 FPU Error
ISR_ERRCODE   17  ; Alignment Check
ISR_NOERRCODE 18  ; Machine Check
ISR_NOERRCODE 19  ; SIMD Floating-Point Exception
ISR_NOERRCODE 20  ; Virtualization Exception
ISR_NOERRCODE 21  ; Control Protection Exception
ISR_NOERRCODE 22  ; Reserved
ISR_NOERRCODE 23  ; Reserved
ISR_NOERRCODE 24  ; Reserved
ISR_NOERRCODE 25  ; Reserved
ISR_NOERRCODE 26  ; Reserved
ISR_NOERRCODE 27  ; Reserved
ISR_NOERRCODE 28  ; Hypervisor Injection Exception
ISR_NOERRCODE 29  ; VMM Communication Exception
ISR_ERRCODE   30  ; Security Exception
ISR_NOERRCODE 31  ; Reserved

; Function to load the IDT
global idt_load
idt_load:
    mov eax, [esp+4]  ; Get the address of the IDT pointer from stack
    lidt [eax]        ; Load the IDT pointer
    ret

; IRQ handlers (ISRs 32-47)
; These are hardware interrupts from the PICs.
; ISRs 32-39 correspond to IRQs 0-7 (Master PIC).
; ISRs 40-47 correspond to IRQs 8-15 (Slave PIC).

%macro ISR_IRQ_NOERRCODE 1
global isr%1
isr%1:
    cli             ; Disable further interrupts immediately
    push byte 0     ; Push a dummy error code (IRQs don't produce one)
    push byte %1    ; Push the interrupt number
    jmp common_irq_stub  ; Jump to the common IRQ handler code
%endmacro

ISR_IRQ_NOERRCODE 32  ; IRQ0  (Programmable Interval Timer)
ISR_IRQ_NOERRCODE 33  ; IRQ1  (Keyboard)
ISR_IRQ_NOERRCODE 34  ; IRQ2  (Cascade for slave PIC)
ISR_IRQ_NOERRCODE 35  ; IRQ3  (Serial Port COM2)
ISR_IRQ_NOERRCODE 36  ; IRQ4  (Serial Port COM1)
ISR_IRQ_NOERRCODE 37  ; IRQ5  (LPT2)
ISR_IRQ_NOERRCODE 38  ; IRQ6  (Floppy Disk)
ISR_IRQ_NOERRCODE 39  ; IRQ7  (LPT1 / Spurious IRQ7)
ISR_IRQ_NOERRCODE 40  ; IRQ8  (CMOS Real-time clock)
ISR_IRQ_NOERRCODE 41  ; IRQ9  (Free for peripherals / legacy SCSI / NIC)
ISR_IRQ_NOERRCODE 42  ; IRQ10 (Free for peripherals / SCSI / NIC)
ISR_IRQ_NOERRCODE 43  ; IRQ11 (Free for peripherals / SCSI / NIC)
ISR_IRQ_NOERRCODE 44  ; IRQ12 (PS/2 Mouse)
ISR_IRQ_NOERRCODE 45  ; IRQ13 (FPU / Coprocessor / Inter-processor)
ISR_IRQ_NOERRCODE 46  ; IRQ14 (Primary ATA Hard Disk)
ISR_IRQ_NOERRCODE 47  ; IRQ15 (Secondary ATA Hard Disk / Spurious IRQ15)

extern irq_handler_common ; C function to handle IRQs

common_irq_stub:
    pusha                 ; Push all general purpose registers (eax, ecx, edx, ebx, esp, ebp, esi, edi)

    mov ax, ds            ; Get current data segment selector
    push eax              ; Save it (as part of registers_t)

    mov ax, 0x10          ; Load kernel data segment selector (should be 0x10 if GDT is setup as: NULL, KERNEL_CODE, KERNEL_DATA)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax            ; Set up segment registers for C kernel space

    cld                   ; Clear direction flag, good practice for C calls

    push esp              ; Push pointer to the registers structure on the stack
    call irq_handler_common ; Call the C part of the IRQ handler
    add esp, 4            ; Clean up pushed pointer (arg for irq_handler_common)

    pop eax               ; Restore original data segment selector to AX (value pushed was from ds)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax            ; Restore segment registers

    popa                  ; Restore all general purpose registers
    add esp, 8            ; Clean up the pushed error code (dummy) and interrupt number
    iret                  ; Return from interrupt (restores EIP, CS, EFLAGS, and optionally UserESP, UserSS)
