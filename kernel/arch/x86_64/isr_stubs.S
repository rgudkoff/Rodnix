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
    ; XNU-style: Save all registers in EXACT order matching struct registers
    ; struct: r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rsp_orig, rbx, rdx, rcx, rax
    ; XNU approach: Save rax first, use it to compute rsp_orig, restore it from stack, save it last
    
    ; Save rax first (temporarily, we'll use it to compute rsp_orig)
    push rax
    
    ; Save general-purpose registers in struct order (except rax and rsp_orig)
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    
    ; Compute rsp_orig: current_rsp + 12 registers (96) + interrupt frame (40) = current_rsp + 136
    ; Interrupt frame: error_code (8) + int_no (8) + rip (8) + cs (8) + rflags (8) = 40 bytes
    ; We've pushed: rax, r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp = 12 registers (96 bytes)
    mov rax, rsp
    add rax, 136           ; Skip: 12 registers (96) + interrupt frame (40) = 136
    push rax               ; Save rsp_orig
    
    push rbx
    push rdx
    push rcx
    
    ; Get original rax from stack and save it last (as per struct order)
    ; Stack now: [top] rcx, rdx, rbx, rsp_orig, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, rax_orig [bottom]
    ; Original rax is at [rsp + 15*8] (skip rcx, rdx, rbx, rsp_orig, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15)
    mov rax, [rsp + 15*8]  ; Get original rax
    push rax               ; Save original rax (last in struct)
    
    ; Save segment registers (not in struct, but we need to restore them)
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
    
    ; DIAGNOSTIC: Mark before calling C handler (write to VGA, bottom of screen, RED)
    mov rax, 0xB8000
    mov word [rax + 80 * 24 * 2], 0x0C00 | ('S')  ; 'S' for stub (RED)
    mov word [rax + 80 * 24 * 2 + 2], 0x0C00 | ('T')  ; 'T' for stub (RED)
    
    ; Call C handler (EOI is sent in irq_handler)
    mov rdi, rsp    ; Pass pointer to registers
    call irq_handler
    
    ; DIAGNOSTIC: Mark after calling C handler (RED)
    mov rax, 0xB8000
    mov word [rax + 80 * 24 * 2 + 4], 0x0C00 | ('D')  ; 'D' for done (RED)
    mov word [rax + 80 * 24 * 2 + 6], 0x0C00 | ('R')  ; 'R' for return (RED)
    
irq_done:
    ; DIAGNOSTIC: Mark before iretq (RED)
    mov rax, 0xB8000
    mov word [rax + 80 * 24 * 2 + 8], 0x0C00 | ('I')  ; 'I' for iretq (RED)
    
    ; Restore segment registers
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; XNU-style: Restore all registers in reverse order (exact reverse of struct order)
    ; Stack: [top] rax, rcx, rdx, rbx, rsp_orig, rbp, rsi, rdi, r8...r15, rax_orig [bottom]
    ; Note: rax was saved twice - first (rax_orig) and last (rax). We restore from last.
    pop rax                 ; rax (last in struct) - this is the correct rax
    pop rcx                 ; rcx
    pop rdx                 ; rdx
    pop rbx                 ; rbx
    add rsp, 8              ; Skip rsp_orig (we don't need it)
    pop rbp                 ; rbp
    pop rsi                 ; rsi
    pop rdi                 ; rdi
    pop r8                   ; r8
    pop r9                   ; r9
    pop r10                  ; r10
    pop r11                  ; r11
    pop r12                  ; r12
    pop r13                  ; r13
    pop r14                  ; r14
    pop r15                  ; r15
    ; Skip original rax (rax_orig) - it was saved first but we already restored rax from last save
    add rsp, 8              ; Skip duplicate rax_orig
    
    ; Remove error code and interrupt number
    ; Stack now should have: RIP, CS, RFLAGS (for kernel mode, no RSP/SS)
    add rsp, 16
    
    ; DIAGNOSTIC: Mark before iretq (RED)
    mov rax, 0xB8000
    mov word [rax + 80 * 24 * 2 + 10], 0x0C00 | ('Q')  ; 'Q' for iretq (RED)
    
    ; Return from interrupt
    ; Stack should have: RIP (8), CS (8), RFLAGS (8)
    iretq
    
    ; DIAGNOSTIC: This should never be reached, but mark it anyway (RED)
    mov rax, 0xB8000
    mov word [rax + 80 * 24 * 2 + 12], 0x0C00 | ('!')  ; '!' for error (RED)

