default rel

section .text

global x86_64_syscall_fast_entry
extern x86_64_syscall_fast_dispatch_frame

; struct percpu offsets, mirrored by _Static_assert in
; kernel/arch/x86_64/percpu.h. GS.Base points at this CPU's slot, so these
; reads answer for the processor executing them and never for another.
%define PCPU_SELF             0
%define PCPU_TSS_RSP0         8
%define PCPU_SYSCALL_USER_RSP 16
%define PCPU_TLS_FS_BASE      24

%define TF_GS       0
%define TF_FS       8
%define TF_ES       16
%define TF_DS       24
%define TF_R15      32
%define TF_R14      40
%define TF_R13      48
%define TF_R12      56
%define TF_R11      64
%define TF_R10      72
%define TF_R9       80
%define TF_R8       88
%define TF_RBP      96
%define TF_RDI      104
%define TF_RSI      112
%define TF_RDX      120
%define TF_RCX      128
%define TF_RBX      136
%define TF_RAX      144
%define TF_INTNO    152
%define TF_ERR      160
%define TF_RIP      168
%define TF_CS       176
%define TF_RFLAGS   184
%define TF_RSP      192
%define TF_SS       200
%define TF_SIZE     208

x86_64_syscall_fast_entry:
    ; On SYSCALL entry: rsp=user_rsp, rcx=user_rip, r11=user_rflags.
    mov [gs:PCPU_SYSCALL_USER_RSP], rsp
    mov rsp, [gs:PCPU_TSS_RSP0]
    ; Keep ABI-safe alignment regardless of rsp0 source alignment.
    and rsp, -16

    ; For C calls SysV requires 16-byte alignment *before* call.
    sub rsp, TF_SIZE
    mov [rsp + TF_RAX], rax

    ; Segment snapshot (RodNIX keeps flat segments, save visible selectors).
    mov rax, ds
    mov [rsp + TF_DS], rax
    mov rax, es
    mov [rsp + TF_ES], rax
    mov rax, fs
    mov [rsp + TF_FS], rax
    mov rax, gs
    mov [rsp + TF_GS], rax

    ; Full GPR snapshot.
    mov [rsp + TF_R15], r15
    mov [rsp + TF_R14], r14
    mov [rsp + TF_R13], r13
    mov [rsp + TF_R12], r12
    mov [rsp + TF_R11], r11
    mov [rsp + TF_R10], r10
    mov [rsp + TF_R9],  r9
    mov [rsp + TF_R8],  r8
    mov [rsp + TF_RBP], rbp
    mov [rsp + TF_RDI], rdi
    mov [rsp + TF_RSI], rsi
    mov [rsp + TF_RDX], rdx
    ; SYSCALL stores user RIP in RCX, syscall arg4 is in R10.
    mov [rsp + TF_RCX], r10
    mov [rsp + TF_RBX], rbx

    ; Metadata + user return context.
    mov qword [rsp + TF_INTNO], 0x80
    mov qword [rsp + TF_ERR], 0
    mov [rsp + TF_RIP], rcx
    mov qword [rsp + TF_CS], 0x23
    mov [rsp + TF_RFLAGS], r11
    mov rax, [gs:PCPU_SYSCALL_USER_RSP]
    mov [rsp + TF_RSP], rax
    mov qword [rsp + TF_SS], 0x1b

    mov rdi, rsp
    call x86_64_syscall_fast_dispatch_frame

    ; Restore full register state from trapframe, keeping returned rax.
    mov r15, [rsp + TF_R15]
    mov r14, [rsp + TF_R14]
    mov r13, [rsp + TF_R13]
    mov r12, [rsp + TF_R12]
    mov r10, [rsp + TF_R10]
    mov r9,  [rsp + TF_R9]
    mov r8,  [rsp + TF_R8]
    mov rbp, [rsp + TF_RBP]
    mov rdi, [rsp + TF_RDI]
    mov rsi, [rsp + TF_RSI]
    mov rdx, [rsp + TF_RDX]
    mov rbx, [rsp + TF_RBX]

    ; Re-apply userspace FS.Base before SYSRET. Linux guests rely on TLS in the
    ; first instructions after syscall return (e.g. ash child after fork). Even
    ; on the fast path the hidden FS base can end up stale/zero after nested
    ; kernel activity, so restore it from the scheduler-maintained shadow.
    mov ecx, 0xC0000100
    mov rax, [gs:PCPU_TLS_FS_BASE]
    mov rdx, rax
    shr rdx, 32
    wrmsr

    mov rax, [rsp + TF_RAX]

    mov rcx, [rsp + TF_RIP]
    mov r11, [rsp + TF_RFLAGS]
    mov rdx, [rsp + TF_RSP]
    mov rsp, rdx

    ; 64-bit SYSRETQ opcode (REX.W + SYSRET).
    db 0x48, 0x0F, 0x07
