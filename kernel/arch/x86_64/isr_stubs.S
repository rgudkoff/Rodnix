; isr_stubs.S - ISR (Interrupt Service Routine) stubs for x86_64
; These are assembly wrappers that call C handlers

default rel

; struct percpu offsets, mirrored by _Static_assert in
; kernel/arch/x86_64/percpu.h. GS.Base points at this CPU's slot, so these
; reads answer for the processor executing them and never for another.
%define PCPU_SELF             0
%define PCPU_TSS_RSP0         8
%define PCPU_SYSCALL_USER_RSP 16
%define PCPU_TLS_FS_BASE      24
%define PCPU_IRET_RSP         32
%define PCPU_IRET_RIP         40
%define PCPU_IRET_CS          48
%define PCPU_IRET_RFLAGS      56
%define PCPU_PREV_ONCPU       64

section .text

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push 0                 ; Dummy error code
    push %1                ; Interrupt number
    jmp interrupt_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push %1                ; Interrupt number (error code already on stack)
    jmp interrupt_common_stub
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

; Syscall handler (vector 128 / 0x80)
ISR_NOERRCODE 128

; Hardware interrupt handlers (32-255)
%macro IRQ_VECTOR 1
global irqvec%1
irqvec%1:
    push 0                 ; Dummy error code
    push %1                ; Interrupt vector number
    jmp interrupt_common_stub
%endmacro

%assign i 32
%rep 224
    %if i != 128
        IRQ_VECTOR i
    %endif
    %assign i i + 1
%endrep

global irq_stub_table
irq_stub_table:
%assign i 32
%rep 224
    %if i == 128
        dq isr128
    %else
        dq irqvec %+ i
    %endif
    %assign i i + 1
%endrep

; Common ISR stub
extern interrupt_entry
; The single entry point for every vector: exceptions, hardware interrupts
; and the software traps alike. There used to be two of these, byte-identical
; apart from a comment, with two equally identical C entry points behind them
; -- 120 lines of the most safety-critical assembly in the kernel that had to
; be edited twice by hand every time and would have diverged silently.
; See docs/ru/irq_audit.md, F2.
interrupt_common_stub:
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
    
    ; Load kernel data segments.
    ;
    ; GS is deliberately left alone. Writing any selector to GS in 64-bit
    ; mode zeroes the hidden base, and IA32_GS_BASE is what addresses this
    ; CPU's struct percpu (kernel/arch/x86_64/percpu.h) -- the C handler
    ; below reads it for the current thread and task. The selector itself is
    ; ignored for addressing in 64-bit flat mode, so there is nothing to
    ; gain by reloading it and a per-CPU pointer to lose.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    
    ; Call C handler
    mov rdi, rsp    ; Pass pointer to registers
    call interrupt_entry
    ; Optional context switch: handler returns new frame in rax
    test rax, rax
    jz interrupt_done
    mov rsp, rax

    ; The stack switch is now complete, and this is the first instant at
    ; which the thread we came from is provably no longer being executed on.
    ; Release it: a processor that pulled it out of the run queue is spinning
    ; on exactly this word, because until now its kernel stack was still in
    ; use here. Doing it any earlier in C would leave a window in which two
    ; processors run on one stack.
    mov rcx, [gs:PCPU_PREV_ONCPU]
    test rcx, rcx
    jz interrupt_done
    mov qword [gs:PCPU_PREV_ONCPU], 0
    ; x86 stores are release-ordered, so no fence is needed for the waiter to
    ; see the stack pointer written before this.
    mov dword [rcx], 0
    
interrupt_done:
    ; Decide return CPL by inspecting saved CS
    mov rax, [rsp + 176]   ; saved CS in CPU frame
    test al, 0x3
    jz .kernel_return
    ; User return: restore original segment registers from stack
    mov rax, [rsp + 24]    ; ds
    mov ds, ax
    mov rax, [rsp + 16]    ; es
    mov es, ax
    mov rax, [rsp + 8]     ; fs
    mov fs, ax
    ; GS is not restored: it was never modified on entry, so it already holds
    ; the value userspace had, and writing it would zero the kernel's GS base.
    ; The saved slot stays in the frame so the layout shared with
    ; syscall_fast_entry.S and interrupt_frame.h is untouched.
    add rsp, 32
    jmp .after_segs
.kernel_return:
    ; Kernel return: restore kernel data selector
    add rsp, 32
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
.after_segs:
    ; Re-apply FS.Base MSR.  Loading a segment selector in 64-bit mode zeroes
    ; the hidden base field, so the "mov fs, [saved_user_sel]" above cleared
    ; whatever sched_arch_apply_thread() set.  rax/rdx/ecx are free here —
    ; their true values are still on the stack and will be restored by pops below.
    mov    ecx, 0xC0000100
    mov    rax, [gs:PCPU_TLS_FS_BASE]
    mov    rdx, rax
    shr    rdx, 32
    wrmsr

    ; Diagnostic capture: compute iretq stack values before restoring regs
    lea rdx, [rsp + 136]   ; 15 regs * 8 + 16 (int_no + err_code)
    mov [gs:PCPU_IRET_RSP], rdx
    mov rax, [rdx]
    mov [gs:PCPU_IRET_RIP], rax
    mov rax, [rdx + 8]
    mov [gs:PCPU_IRET_CS], rax
    mov rax, [rdx + 16]
    mov [gs:PCPU_IRET_RFLAGS], rax

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
