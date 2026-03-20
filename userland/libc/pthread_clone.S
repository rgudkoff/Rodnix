; pthread_clone.S — NASM x86_64 assembly trampoline for pthread_create
;
; __pthread_do_clone(flags, child_stack, ptid, ctid, newtls, start_fn, start_arg)
;
; System V AMD64 ABI register mapping:
;   rdi = flags
;   rsi = child_stack   (top of new thread's stack)
;   rdx = ptid
;   rcx = ctid
;   r8  = newtls        (FS.Base for new thread)
;   r9  = start_fn
;   [rsp+8] = start_arg (7th argument, stack-passed)
;
; We push start_fn and start_arg onto the child's stack before the syscall.
; After clone() returns 0 in the child:
;   rsp = adjusted child_stack
;   pop rdi  -> start_arg (first argument)
;   pop rax  -> start_fn
;   call rax -> start_fn(start_arg)
;
; Kernel syscall ABI: nr=rax, a1=rdi, a2=rsi, a3=rdx, a4=r10, a5=r8
; POSIX_SYS_CLONE = 78

section .text

global __pthread_do_clone

__pthread_do_clone:
    ; Load start_arg (7th argument at [rsp+8]).
    mov    r11, [rsp + 8]

    ; Push start_fn then start_arg onto the child's stack (grows downward).
    sub    rsi, 8
    mov    [rsi], r9        ; [new_top+8] = start_fn
    sub    rsi, 8
    mov    [rsi], r11       ; [new_top]   = start_arg

    ; Align child stack to 16 bytes (ABI requirement).
    and    rsi, -16

    ; Move ctid to r10 (syscall a4 register; rcx is clobbered by `syscall`).
    mov    r10, rcx

    ; Invoke clone syscall.
    ; rdi=flags, rsi=child_stack, rdx=ptid, r10=ctid, r8=newtls
    mov    rax, 78          ; POSIX_SYS_CLONE
    int    0x80             ; use int $0x80 (not syscall) for simplicity

    ; Parent: rax != 0 (tid or error)
    test   rax, rax
    jnz    .parent

    ; ---- Child path ----
    ; rsp = adjusted child_stack, with start_fn and start_arg on top.
    xor    rbp, rbp         ; clear frame pointer
    pop    rdi              ; rdi = start_arg
    pop    rax              ; rax = start_fn
    call   rax              ; start_fn(start_arg)  -- should not return

    ; Fallback: exit thread if start_fn returns.
    mov    rax, 80          ; POSIX_SYS_THREAD_EXIT
    xor    rdi, rdi
    int    0x80
.hang:
    jmp    .hang

    ; ---- Parent path ----
.parent:
    ret
