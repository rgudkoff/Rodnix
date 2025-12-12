; isr_stubs.S - ISR (Interrupt Service Routine) stubs for x86_64
; These are assembly wrappers that call C handlers

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0                 ; Dummy error code
    push %1                ; Interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1                ; Interrupt number (error code already on stack)
    jmp isr_common_stub
%endmacro

; Exception handlers (0-31)
ISR_NOERRCODE 0   ; Division by zero
ISR_NOERRCODE 1   ; Debug
ISR_NOERRCODE 2   ; Non-maskable interrupt
ISR_NOERRCODE 3   ; Breakpoint
ISR_NOERRCODE 4   ; Overflow
ISR_NOERRCODE 5   ; Bound range exceeded
ISR_NOERRCODE 6   ; Invalid opcode
ISR_NOERRCODE 7   ; Device not available
ISR_ERRCODE   8   ; Double fault
ISR_NOERRCODE 9   ; Coprocessor segment overrun
ISR_ERRCODE   10  ; Invalid TSS
ISR_ERRCODE   11  ; Segment not present
ISR_ERRCODE   12  ; Stack-segment fault
ISR_ERRCODE   13  ; General protection fault
ISR_ERRCODE   14  ; Page fault
ISR_NOERRCODE 15  ; Reserved
ISR_NOERRCODE 16  ; x87 FPU error
ISR_ERRCODE   17  ; Alignment check
ISR_NOERRCODE 18  ; Machine check
ISR_NOERRCODE 19  ; SIMD floating-point exception
ISR_NOERRCODE 20  ; Virtualization exception
ISR_NOERRCODE 21  ; Control protection exception
ISR_NOERRCODE 22  ; Reserved
ISR_NOERRCODE 23  ; Reserved
ISR_NOERRCODE 24  ; Reserved
ISR_NOERRCODE 25  ; Reserved
ISR_NOERRCODE 26  ; Reserved
ISR_NOERRCODE 27  ; Reserved
ISR_NOERRCODE 28  ; Hypervisor injection exception
ISR_NOERRCODE 29  ; VMM communication exception
ISR_ERRCODE   30  ; Security exception
ISR_NOERRCODE 31  ; Reserved

; IRQ handlers (32-47)
%macro IRQ 1
global irq%1
irq%1:
    push 0                 ; Dummy error code
    push %1 + 32           ; IRQ number (32-47)
    jmp irq_common_stub
%endmacro

IRQ 0   ; Timer
IRQ 1   ; Keyboard
IRQ 2   ; Cascade
IRQ 3   ; COM2
IRQ 4   ; COM1
IRQ 5   ; LPT2
IRQ 6   ; Floppy
IRQ 7   ; LPT1
IRQ 8   ; CMOS clock
IRQ 9   ; Free
IRQ 10  ; Free
IRQ 11  ; Free
IRQ 12  ; PS/2 mouse
IRQ 13  ; FPU
IRQ 14  ; Primary ATA
IRQ 15  ; Secondary ATA

; Common ISR stub
extern isr_handler
isr_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Save segment registers
    mov rax, ds
    push rax
    mov rax, es
    push rax
    mov rax, fs
    push rax
    mov rax, gs
    push rax
    
    ; Load kernel data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Call C handler
    mov rdi, rsp    ; Pass pointer to registers
    call isr_handler
    
    ; Restore segment registers
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Remove error code and interrupt number
    add rsp, 16
    
    ; Return from interrupt
    iretq

; Common IRQ stub
extern irq_handler
irq_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Save segment registers
    mov rax, ds
    push rax
    mov rax, es
    push rax
    mov rax, fs
    push rax
    mov rax, gs
    push rax
    
    ; Load kernel data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Call C handler
    mov rdi, rsp    ; Pass pointer to registers
    call irq_handler
    
    ; Send EOI to PIC
    mov al, 0x20
    out 0x20, al    ; Master PIC
    cmp byte [rsp + 16 * 8], 40  ; Check if IRQ >= 8
    jl irq_done
    out 0xA0, al    ; Slave PIC
    
irq_done:
    ; Restore segment registers
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Remove error code and interrupt number
    add rsp, 16
    
    ; Return from interrupt
    iretq

