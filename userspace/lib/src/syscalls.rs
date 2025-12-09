// Syscall interface for userspace

pub unsafe fn syscall0(n: usize) -> usize {
    let ret: usize;
    core::arch::asm!(
        "int 0x80",
        in("eax") n,
        lateout("eax") ret,
        options(nostack, preserves_flags)
    );
    ret
}

pub unsafe fn syscall1(n: usize, arg1: usize) -> usize {
    let ret: usize;
    core::arch::asm!(
        "int 0x80",
        in("eax") n,
        in("ebx") arg1,
        lateout("eax") ret,
        options(nostack, preserves_flags)
    );
    ret
}

pub unsafe fn syscall2(n: usize, arg1: usize, arg2: usize) -> usize {
    let ret: usize;
    core::arch::asm!(
        "int 0x80",
        in("eax") n,
        in("ebx") arg1,
        in("ecx") arg2,
        lateout("eax") ret,
        options(nostack, preserves_flags)
    );
    ret
}

pub unsafe fn syscall3(n: usize, arg1: usize, arg2: usize, arg3: usize) -> usize {
    let ret: usize;
    core::arch::asm!(
        "int 0x80",
        in("eax") n,
        in("ebx") arg1,
        in("ecx") arg2,
        in("edx") arg3,
        lateout("eax") ret,
        options(nostack, preserves_flags)
    );
    ret
}

pub unsafe fn syscall4(n: usize, arg1: usize, arg2: usize, arg3: usize, arg4: usize) -> usize {
    let ret: usize;
    core::arch::asm!(
        "int 0x80",
        in("eax") n,
        in("ebx") arg1,
        in("ecx") arg2,
        in("edx") arg3,
        in("esi") arg4,
        lateout("eax") ret,
        options(nostack, preserves_flags)
    );
    ret
}

pub unsafe fn syscall5(n: usize, arg1: usize, arg2: usize, arg3: usize, arg4: usize, arg5: usize) -> usize {
    let ret: usize;
    core::arch::asm!(
        "int 0x80",
        in("eax") n,
        in("ebx") arg1,
        in("ecx") arg2,
        in("edx") arg3,
        in("esi") arg4,
        in("edi") arg5,
        lateout("eax") ret,
        options(nostack, preserves_flags)
    );
    ret
}

// Syscall numbers
pub const SYS_EXIT: usize = 1;
pub const SYS_READ: usize = 2;
pub const SYS_WRITE: usize = 3;
pub const SYS_IPC_SEND: usize = 4;
pub const SYS_IPC_RECV: usize = 5;
pub const SYS_COPY_TO_USER: usize = 6;
pub const SYS_COPY_FROM_USER: usize = 7;

