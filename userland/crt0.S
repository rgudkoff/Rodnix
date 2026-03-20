global _start
extern main
extern environ

; C++ global constructor/destructor table symbols (defined in link.ld)
extern __init_array_start
extern __init_array_end
extern __ctors_start
extern __ctors_end

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

    ; Call C++ global constructors (.init_array — new ABI, forward order).
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

    ; Call C++ global constructors (.ctors — old ELF ABI used by x86_64-elf-g++,
    ; reverse order: walk from __ctors_end back to __ctors_start).
    lea  rbx, [rel __ctors_end]
    lea  r12, [rel __ctors_start]
.ctors_loop:
    cmp  rbx, r12
    jle  .ctors_done
    sub  rbx, 8
    mov  rax, [rbx]
    test rax, rax
    jz   .ctors_loop
    call rax
    jmp  .ctors_loop
.ctors_done:

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
