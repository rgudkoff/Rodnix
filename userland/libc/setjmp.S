; setjmp / longjmp for x86_64 RodNIX userland (NASM syntax).
;
; jmp_buf layout (8 × 8 bytes = 64 bytes):
;   [0*8]  rbx   [1*8]  rbp   [2*8]  r12   [3*8]  r13
;   [4*8]  r14   [5*8]  r15   [6*8]  rsp   [7*8]  rip

global setjmp
global longjmp

section .text

; int setjmp(jmp_buf env)
; rdi = env
setjmp:
    mov  [rdi + 0],  rbx
    mov  [rdi + 8],  rbp
    mov  [rdi + 16], r12
    mov  [rdi + 24], r13
    mov  [rdi + 32], r14
    mov  [rdi + 40], r15
    lea  rax, [rsp + 8]         ; caller rsp (before call to setjmp)
    mov  [rdi + 48], rax
    mov  rax, [rsp]             ; return address (rip)
    mov  [rdi + 56], rax
    xor  eax, eax
    ret

; void longjmp(jmp_buf env, int val)
; rdi = env, esi = val
longjmp:
    mov  eax, esi
    test eax, eax
    jnz  .val_ok
    mov  eax, 1
.val_ok:
    mov  rbx, [rdi + 0]
    mov  rbp, [rdi + 8]
    mov  r12, [rdi + 16]
    mov  r13, [rdi + 24]
    mov  r14, [rdi + 32]
    mov  r15, [rdi + 40]
    mov  rsp, [rdi + 48]
    mov  rcx, [rdi + 56]       ; return address
    jmp  rcx
