global _start
extern main

section .text
_start:
    call main
    mov rdi, rax
    mov rax, 15
    int 0x80
.hang:
    pause
    jmp .hang
