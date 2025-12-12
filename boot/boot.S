; boot.S - Boot code for x86_64
; Multiboot2 bootloader entry point and 64-bit mode switch

; Multiboot2 constants
%define MB2_MAGIC   0xE85250D6
%define MB2_ARCH    0
%define MB2_LEN     (mb2_end - mb2_start)
%define MB2_CSUM    -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

; MSR constants
%define EFER_MSR    0xC0000080
%define EFER_LME    (1 << 8)
%define EFER_NXE    (1 << 11)

; CR0 constants
%define CR0_PE      (1 << 0)
%define CR0_PG      (1 << 31)
%define CR0_WP      (1 << 16)

; CR4 constants
%define CR4_PAE     (1 << 5)

; Multiboot2 header
section .multiboot_header
align 8
mb2_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_LEN
    dd MB2_CSUM
    ; tag_end (type=0, size=8)
    dd 0
    dd 8
mb2_end:

global start
extern kmain

; Early stacks
section .bss
align 16
stack32_bottom:
    resb 32*1024
global stack32_top
stack32_top:

align 16
stack64_bottom:
    resb 32*1024
global stack64_top
stack64_top:

; Early page tables (aligned, in .bss)
align 4096
global pml4
pml4:
    resq 512

align 4096
global pdpt0
pdpt0:
    resq 512

align 4096
global pd0
pd0:
    resq 512   ; 2MiB pages for first 1GiB (512 entries)

; Tiny GDT (null, code64, data)
section .rodata
align 16
gdt64:
    dq 0
    ; code: base=0, limit=0, L=1, D=0, G=1
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xA0
    db 0x00
    ; data: base=0, limit=0, L=0, D=1, G=1
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xC0
    db 0x00
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64
    dd 0  ; upper 32 bits (not used in 32-bit mode)

; 32-bit entry (Multiboot2 hands us protected mode)
section .text.start
bits 32
start:
    cli

    ; Save multiboot2 registers
    mov  [mb_magic], eax
    mov  [mb_mbi],   ebx

    ; Set 32-bit stack
    mov  esp, stack32_top

    ; Build identity maps: 1GiB via 2MiB pages
    call build_identity_1g

    ; Enable PAE
    mov  eax, cr4
    or   eax, CR4_PAE
    mov  cr4, eax

    ; Load CR3 with PML4 base (physical)
    mov  eax, pml4
    mov  cr3, eax

    ; Enable LME + NXE
    mov  ecx, EFER_MSR
    rdmsr                 ; EDX:EAX = EFER
    or   eax, EFER_LME | EFER_NXE
    wrmsr

    ; Enable paging and write-protect
    mov  eax, cr0
    or   eax, CR0_PG | CR0_WP | CR0_PE
    mov  cr0, eax

    ; Load temporary GDT
    lgdt [gdt64_ptr]

    ; Far jump into 64-bit code segment
    push dword 0x08
    push dword long_mode_entry
    retf

; Page table builder: PML4[0] -> PDPT0[0] -> PD0 (2MiB pages)
build_identity_1g:
    ; Zero tables
    xor  eax, eax
    mov  edi, pml4
    mov  ecx, 512*8/4
    rep  stosd

    mov  edi, pdpt0
    mov  ecx, 512*8/4
    rep  stosd

    mov  edi, pd0
    mov  ecx, 512*8/4
    rep  stosd
        
    ; PML4[0] -> PDPT0 | P=1, RW=1
    mov  eax, pdpt0
    or   eax, 0x003
    mov  [pml4 + 0*8], eax
    mov  dword [pml4 + 0*8 + 4], 0

    ; PDPT0[0] -> PD0 | P=1, RW=1
    mov  eax, pd0
    or   eax, 0x003
    mov  [pdpt0 + 0*8], eax
    mov  dword [pdpt0 + 0*8 + 4], 0

    ; Fill PD0 with 2MiB mappings (PS=1)
    mov  ecx, 512
    xor  edx, edx             ; edx:eax = phys base for entry
    mov  edi, pd0
.map2m:
    ; entry = phys | P=1|RW=1|PS=1 (bit 7)
    mov  eax, edx
    or   eax, 0x183           ; 1=Present, 2=RW, 0x80=PS
    mov  [edi], eax
    mov  dword [edi+4], 0
    add  edx, 0x200000        ; next 2MiB
    add  edi, 8
    loop .map2m
    ret

; 64-bit entry
section .text64
bits 64
long_mode_entry:
    ; Set data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Switch to 64-bit stack
    lea rsp, [rel stack64_top]

    ; Pass multiboot2 magic and mbi to kmain as (RDI, RSI)
    mov edi, dword [rel mb_magic]
    mov rsi, qword [rel mb_mbi]

    ; Clear direction flag
    cld

    ; Call kmain(uint32_t magic, void* mbi)
    lea rax, [rel kmain]
    call rax
        
.hang:
    hlt
    jmp .hang

section .bss
align 8
mb_magic:
    resd 1
mb_mbi:
    resq 1

