//! rust_echo — minimal Rust echo utility for RodNIX.
//!
//! Keeps the same MVP behavior as the current C `/bin/echo`:
//! print argv[1..] separated by spaces and terminate with `\n`.

#![no_std]
#![no_main]

use core::arch::asm;

const SYS_WRITE: usize = 13;
const STDOUT: usize = 1;
const STDERR: usize = 2;

unsafe fn sys_write(fd: usize, buf: *const u8, len: usize) -> isize {
    let ret: isize;
    asm!(
        "int 0x80",
        inout("rax") SYS_WRITE => ret,
        in("rdi") fd,
        in("rsi") buf,
        in("rdx") len,
        options(nostack, preserves_flags),
    );
    ret
}

fn write_bytes(fd: usize, buf: &[u8]) {
    if buf.is_empty() {
        return;
    }

    let mut written = 0usize;
    while written < buf.len() {
        let ret = unsafe { sys_write(fd, buf.as_ptr().add(written), buf.len() - written) };
        if ret <= 0 {
            break;
        }
        written += ret as usize;
    }
}

unsafe fn cstr_bytes<'a>(ptr: *const u8) -> &'a [u8] {
    if ptr.is_null() {
        return &[];
    }

    let mut len = 0usize;
    while *ptr.add(len) != 0 {
        len += 1;
    }
    core::slice::from_raw_parts(ptr, len)
}

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    if argc > 1 {
        let argc_usize = argc as usize;
        let mut i = 1usize;
        while i < argc_usize {
            let arg = unsafe { cstr_bytes(*argv.add(i)) };
            write_bytes(STDOUT, arg);
            if i + 1 < argc_usize {
                write_bytes(STDOUT, b" ");
            }
            i += 1;
        }
    }

    write_bytes(STDOUT, b"\n");
    0
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    write_bytes(STDERR, b"rust_echo: panic\n");
    loop {
        unsafe {
            asm!("pause", options(nomem, nostack));
        }
    }
}
