global _start
extern main

section .text
_start:
    call main
.hang:
    jmp .hang
