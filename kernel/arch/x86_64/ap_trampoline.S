; ap_trampoline.S - wake-up path for application processors.
;
; A STARTUP IPI hands the target processor an 8-bit vector and it begins
; executing at vector << 12, in 16-bit real mode, with no stack, no GDT and no
; paging. None of that is expressible in C, which is why this file exists.
;
; The blob is copied to AP_TRAMPOLINE_PHYS at run time, so every address it
; refers to is computed from that constant at assembly time rather than from
; the link address. That leaves the blob free of relocations -- verifiable
; with readelf -- which is what makes copying it valid at all.
;
; Deliberately no "default rel": in the 64-bit section the patch slots are
; reached by absolute address, and RIP-relative encodings would point back at
; wherever the linker put this code rather than at the running copy.

%define AP_TRAMPOLINE_PHYS 0x8000
%define TRAMP(sym)         (AP_TRAMPOLINE_PHYS + ((sym) - ap_trampoline_start))

; One GDT covers all three modes. The 64-bit far jump only needs the L-bit
; descriptor to be present, not a second table.
%define SEL_CODE32 0x08
%define SEL_DATA   0x10
%define SEL_CODE64 0x18

section .text

global ap_trampoline_start
global ap_trampoline_end
global ap_trampoline_cr3
global ap_trampoline_stack
global ap_trampoline_entry

bits 16
align 4096
ap_trampoline_start:
    cli
    cld

    ; CS is vector<<8 on entry; everything else is undefined. A flat zero
    ; data segment makes the absolute addresses below mean what they say.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    lgdt [TRAMP(ap_gdt_ptr)]

    mov eax, cr0
    or  eax, 1                      ; CR0.PE
    mov cr0, eax

    jmp dword SEL_CODE32:TRAMP(ap_protected)

bits 32
ap_protected:
    mov ax, SEL_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, cr4
    or  eax, 1 << 5                 ; CR4.PAE, required before long mode
    mov cr4, eax

    ; The trampoline's own page tables: identity for low memory, because the
    ; next few instructions still execute at physical addresses, plus the
    ; kernel's high half so the final jump has somewhere to land. Patched by
    ; the C side before the IPI goes out.
    mov eax, [TRAMP(ap_trampoline_cr3)]
    mov cr3, eax

    mov ecx, 0xC0000080             ; IA32_EFER
    rdmsr
    or  eax, 1 << 8                 ; EFER.LME
    wrmsr

    mov eax, cr0
    or  eax, 0x80000001             ; CR0.PG | CR0.PE
    mov cr0, eax

    jmp SEL_CODE64:TRAMP(ap_long)

bits 64
ap_long:
    mov ax, SEL_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; FS and GS are left alone. Writing a selector to GS zeroes the hidden
    ; base, and the C entry sets that base to this CPU's percpu slot.

    ; Enable SSE before any compiled code runs. GCC emits XMM instructions
    ; freely -- memset alone uses movd/punpcklbw -- and an application
    ; processor comes out of reset with CR0.EM set and CR4.OSFXSR clear, so
    ; the first such instruction raises #UD. The boot processor does this in
    ; cpu_init(); an AP cannot, because getting to cpu_init's C code already
    ; requires it. Mirrors the CR0/CR4 bits set in kernel/arch/x86_64/cpu.c.
    mov rax, cr0
    and rax, ~(1 << 2)              ; clear CR0.EM (no x87 emulation)
    or  rax, 1 << 1                 ; set CR0.MP
    mov cr0, rax

    mov rax, cr4
    or  rax, (1 << 9) | (1 << 10)   ; CR4.OSFXSR | CR4.OSXMMEXCPT
    mov cr4, rax

    mov rsp, [abs TRAMP(ap_trampoline_stack)]
    xor rbp, rbp

    ; The kernel lives at 0xFFFFFFFF8..., far outside the reach of a
    ; relative jump from low memory, so the target is loaded as a full
    ; 64-bit immediate and jumped to indirectly.
    mov rax, [abs TRAMP(ap_trampoline_entry)]
    jmp rax

    ; ap_entry() does not return. If it somehow does, stop rather than
    ; execute whatever follows in low memory.
.halt:
    cli
    hlt
    jmp .halt

align 16
ap_gdt:
    dq 0x0000000000000000           ; null
    dq 0x00CF9A000000FFFF           ; 0x08 code32: present, exec/read, 4G
    dq 0x00CF92000000FFFF           ; 0x10 data:   present, read/write, 4G
    dq 0x00AF9A000000FFFF           ; 0x18 code64: present, exec/read, L=1
ap_gdt_end:

ap_gdt_ptr:
    dw ap_gdt_end - ap_gdt - 1
    dd TRAMP(ap_gdt)

; Patch slots. The C side writes these in the copy at AP_TRAMPOLINE_PHYS,
; not here, and rewrites the stack between processors.
align 8
ap_trampoline_cr3:
    dq 0
ap_trampoline_stack:
    dq 0
ap_trampoline_entry:
    dq 0

ap_trampoline_end:
