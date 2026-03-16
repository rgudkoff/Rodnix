global _start
extern main
extern environ

section .text
_start:
    ; Kernel enters native userland with:
    ; rdi=argc, rsi=argv, rdx=envp.
    ; Preserve this ABI and pass through to main(argc, argv, envp).
    mov [rel environ], rdx
    call main
    mov rdi, rax
    ; POSIX_SYS_EXIT
    mov rax, 15
    int 0x80
.hang:
    pause
    jmp .hang
