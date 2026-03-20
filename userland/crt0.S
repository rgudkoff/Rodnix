global _start
extern main
extern environ

; C++ global constructor/destructor table symbols (defined in link.ld)
extern __init_array_start
extern __init_array_end

section .text
_start:
    ; Kernel enters native userland with:
    ; rdi=argc, rsi=argv, rdx=envp.
    ; Preserve this ABI and pass through to main(argc, argv, envp).
    mov [rel environ], rdx

    ; Save argc/argv/envp across constructor calls.
    push rdi
    push rsi
    push rdx

    ; Call C++ global constructors (.init_array).
    lea  rbx, [rel __init_array_start]
    lea  r12, [rel __init_array_end]
.ctor_loop:
    cmp  rbx, r12
    jge  .ctor_done
    mov  rax, [rbx]
    test rax, rax
    jz   .ctor_next
    call rax
.ctor_next:
    add  rbx, 8
    jmp  .ctor_loop
.ctor_done:

    ; Restore argc/argv/envp and call main.
    pop rdx
    pop rsi
    pop rdi
    call main

    mov rdi, rax
    ; POSIX_SYS_EXIT = 15
    mov rax, 15
    int 0x80
.hang:
    pause
    jmp .hang
