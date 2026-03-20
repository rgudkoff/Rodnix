//! rust_hello — first Rust userland utility for RodNIX.
//!
//! Demonstrates the Rust-in-RodNIX pattern:
//!   - no_std / no_main: zero Rust runtime, no allocator required
//!   - raw syscalls via int 0x80 (matching kernel/posix/posix_sysnums.h)
//!   - safe slice iteration over argv
//!   - links against existing crt0.o + userland libc
//!
//! Build: see userland/Makefile target `rust-utils`
//! Run in QEMU: /bin/rust_hello [args...]

#![no_std]
#![no_main]

use core::arch::asm;

// ── syscall numbers (kernel/posix/posix_sysnums.h) ────────────────────────────

const SYS_WRITE: usize = 13;

const STDOUT: usize = 1;
const STDERR: usize = 2;

// ── raw syscall wrappers ───────────────────────────────────────────────────────

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

// ── safe I/O helpers ──────────────────────────────────────────────────────────

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

fn print(s: &str) {
    write_bytes(STDOUT, s.as_bytes());
}

fn eprint(s: &str) {
    write_bytes(STDERR, s.as_bytes());
}

// ── argv helper ───────────────────────────────────────────────────────────────

/// Turn a C string pointer into a `&[u8]` slice (no NUL terminator).
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

/// Iterate over `argv[0..argc]` as byte slices.
unsafe fn argv_iter(argc: i32, argv: *const *const u8) -> impl Iterator<Item = &'static [u8]> {
    (0..argc as usize).map(move |i| cstr_bytes(*argv.add(i)))
}

// ── entry point ───────────────────────────────────────────────────────────────

#[no_mangle]
pub extern "C" fn main(argc: i32, argv: *const *const u8) -> i32 {
    print("rust_hello: hello from Rust on RodNIX\n");

    if argc <= 1 {
        print("usage: rust_hello <arg> [args...]\n");
        return 0;
    }

    // Safely iterate over argv[1..], printing each argument.
    let mut count = 0u32;
    unsafe {
        for arg in argv_iter(argc, argv).skip(1) {
            print("  arg[");
            print_u32(count);
            print("]: ");
            write_bytes(STDOUT, arg);
            print("\n");
            count += 1;
        }
    }

    print("rust_hello: done\n");
    0
}

// ── minimal u32 printer (no alloc, no fmt) ────────────────────────────────────

fn print_u32(mut n: u32) {
    let mut buf = [0u8; 10];
    let mut i = buf.len();
    if n == 0 {
        print("0");
        return;
    }
    while n > 0 {
        i -= 1;
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
    }
    write_bytes(STDOUT, &buf[i..]);
}

// ── panic handler (required by no_std) ───────────────────────────────────────

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    eprint("rust_hello: panic");
    if let Some(loc) = info.location() {
        // Print file:line in a no-alloc way — just file and line.
        eprint(" at ");
        eprint(loc.file());
        eprint(":");
        print_u32(loc.line());
    }
    eprint("\n");
    loop {
        unsafe {
            asm!("pause", options(nomem, nostack));
        }
    }
}
