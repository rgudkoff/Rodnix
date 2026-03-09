/*
 * scstat.c
 * Per-syscall entry statistics (int80 vs fast syscall/sysret).
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "scstat.h"

#define FD_STDOUT 1
#define SCSTAT_CAP 64

static long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

static const char* syscall_name(uint32_t n)
{
    static const char* kNames[] = {
        "nosys", "getpid", "getuid", "geteuid", "getgid", "getegid",
        "setuid", "seteuid", "setgid", "setegid", "open", "close",
        "read", "write", "uname", "exit", "exec", "spawn", "waitpid",
        "readdir", "fcntl", "netiflist", "mmap", "munmap", "brk", "fork",
        "hwlist", "fabricls", "fabricevents", "sysinfo", "clock_gettime",
        "stat", "fstat", "lseek", "scstat", "pipe", "dup", "dup2", "chdir",
        "getcwd", "mkdir", "unlink", "rmdir", "rename", "ioctl", "nanosleep",
        "kill", "sigaction", "sigreturn"
    };
    if (n < (uint32_t)(sizeof(kNames) / sizeof(kNames[0]))) {
        return kNames[n];
    }
    return "?";
}

int main(int argc, char** argv)
{
    int show_all = 0;
    if (argc > 1 && argv && argv[1] && argv[1][0] == '-' && argv[1][1] == 'a') {
        show_all = 1;
    }

    rodnix_scstat_entry_t entries[SCSTAT_CAP];
    uint32_t total = 0;
    long n = posix_scstat(entries, SCSTAT_CAP, &total);
    if (n < 0) {
        (void)write_str("scstat: syscall failed\n");
        return 1;
    }

    (void)write_str("scstat: ");
    write_u64((uint64_t)n);
    (void)write_str("/");
    write_u64((uint64_t)total);
    (void)write_str(" entries\n");

    uint64_t sum_int80 = 0;
    uint64_t sum_fast = 0;
    for (uint32_t i = 0; i < (uint32_t)n; i++) {
        rodnix_scstat_entry_t* e = &entries[i];
        if (!show_all && e->total_count == 0) {
            continue;
        }
        sum_int80 += e->int80_count;
        sum_fast += e->fast_count;
        write_u64((uint64_t)e->syscall_no);
        (void)write_str(" ");
        (void)write_str(syscall_name(e->syscall_no));
        (void)write_str(": int80=");
        write_u64(e->int80_count);
        (void)write_str(" fast=");
        write_u64(e->fast_count);
        (void)write_str(" total=");
        write_u64(e->total_count);
        (void)write_str("\n");
    }

    (void)write_str("sum: int80=");
    write_u64(sum_int80);
    (void)write_str(" fast=");
    write_u64(sum_fast);
    (void)write_str(" total=");
    write_u64(sum_int80 + sum_fast);
    (void)write_str("\n");
    return 0;
}
