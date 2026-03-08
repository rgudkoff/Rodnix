/*
 * init.c
 * Minimal userspace launcher: smoke + exec /bin/sh.
 */

#include <stdint.h>
#include "syscall.h"
#include "posix_syscall.h"
#include "unistd.h"

#define VFS_OPEN_READ 1
#define FD_STDOUT 1
#define SYS_TEST_SLEEP 62

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

static void write_hex_byte(uint8_t b)
{
    char hex[2];
    static const char table[] = "0123456789ABCDEF";
    hex[0] = table[(b >> 4) & 0xF];
    hex[1] = table[b & 0xF];
    (void)write_buf(hex, 2);
}

static void print_file(const char* path)
{
    char buf[64];
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return;
    }
    for (;;) {
        long n = posix_read((int)fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        (void)posix_write(FD_STDOUT, buf, (uint64_t)n);
    }
    (void)posix_close((int)fd);
}

static void print_hostname(void)
{
    char buf[64];
    long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
    if (fd < 0) {
        return;
    }

    long n = posix_read((int)fd, buf, sizeof(buf) - 1);
    (void)posix_close((int)fd);
    if (n <= 0) {
        return;
    }

    int len = (int)n;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        len--;
    }
    if (len <= 0) {
        return;
    }

    (void)write_str("[USER] hostname: ");
    (void)write_buf(buf, (uint64_t)len);
    (void)write_str("\n");
}

static void run_smoke(void)
{
    (void)write_str("[USER] init: POSIX smoke test start\n");
    (void)write_str("[USER] getpid=");
    {
        long pid = posix_getpid();
        if (pid < 0) {
            (void)write_str("ERR\n");
        } else {
            write_u64((uint64_t)pid);
            (void)write_str("\n");
        }
    }

    {
        long fd = posix_open("/bin/init", VFS_OPEN_READ);
        if (fd < 0) {
            (void)write_str("[USER] open('/bin/init') failed\n");
        } else {
            uint8_t hdr[4] = {0, 0, 0, 0};
            long n = posix_read((int)fd, hdr, sizeof(hdr));
            (void)write_str("[USER] read('/bin/init') bytes=");
            if (n < 0) {
                (void)write_str("ERR\n");
            } else {
                write_u64((uint64_t)n);
                (void)write_str(" hdr=");
                for (long i = 0; i < n; i++) {
                    write_hex_byte(hdr[i]);
                }
                (void)write_str("\n");
            }
            if (posix_close((int)fd) < 0) {
                (void)write_str("[USER] close failed\n");
            } else {
                (void)write_str("[USER] close ok\n");
            }
        }
    }

    (void)write_str("[USER] init: POSIX smoke test done\n");
}

static int file_exists(const char* path)
{
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return 0;
    }
    (void)posix_close((int)fd);
    return 1;
}

static void ct_log(const char* id, const char* verdict, const char* msg)
{
    (void)write_str("[CT] ");
    (void)write_str(id);
    (void)write_str(" ");
    (void)write_str(verdict);
    (void)write_str(" ");
    (void)write_str(msg);
    (void)write_str("\n");
}

static void run_contract_mode_if_enabled(void)
{
    if (!file_exists("/etc/contract.auto")) {
        return;
    }

    int ok = 1;
    (void)write_str("[CT] MODE enabled\n");

    {
        char b = 0;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            ct_log("CT-007", "FAIL", "open failed");
            ok = 0;
        } else if (posix_close((int)fd) < 0) {
            ct_log("CT-007", "FAIL", "close failed");
            ok = 0;
        } else if (posix_read((int)fd, &b, 1) < 0) {
            ct_log("CT-007", "PASS", "close invalidates fd");
        } else {
            ct_log("CT-007", "FAIL", "read after close succeeded");
            ok = 0;
        }
    }

    {
        char b = 0;
        if (posix_read(-1, &b, 1) < 0) {
            ct_log("CT-008", "PASS", "read on invalid fd fails");
        } else {
            ct_log("CT-008", "FAIL", "read on invalid fd succeeded");
            ok = 0;
        }
    }

    {
        long ts = rdnx_syscall1(SYS_TEST_SLEEP, 1);
        if (ts >= 0) {
            ct_log("CT-DBG", "PASS", "blocking syscall probe returned");
        } else {
            ct_log("CT-DBG", "FAIL", "blocking syscall probe failed");
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/true", 0);
        if (pid > 0) {
            ct_log("CT-001", "PASS", "spawn creates child pid");
            {
                long wr = waitpid((pid_t)pid, &status, 0);
                if (wr == pid && status == 0) {
                    ct_log("CT-005", "PASS", "waitpid reaps child exit status");
                } else {
                    ct_log("CT-005", "FAIL", "waitpid failed or bad status");
                    ok = 0;
                }
            }
            {
                long wr2 = waitpid((pid_t)pid, &status, 0);
                if (wr2 < 0) {
                    ct_log("CT-006", "PASS", "second waitpid fails after reap");
                } else {
                    ct_log("CT-006", "FAIL", "second waitpid unexpectedly succeeded");
                    ok = 0;
                }
            }
        } else {
            ct_log("CT-001", "FAIL", "spawn failed");
            ct_log("CT-005", "FAIL", "spawn prerequisite failed");
            ct_log("CT-006", "FAIL", "spawn prerequisite failed");
            ok = 0;
        }
    }

    {
        /* Race case: child may exit before parent enters waitpid. */
        int status = -1;
        long pid = posix_spawn("/bin/true", 0);
        if (pid <= 0) {
            ct_log("CT-005", "FAIL", "race spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-005", "PASS", "race fast-exit child reaped");
            } else {
                ct_log("CT-005", "FAIL", "race waitpid failed");
                ok = 0;
            }
        }
    }

    if (ok) {
        (void)write_str("[CT] ALL PASS\n");
    } else {
        (void)write_str("[CT] ALL FAIL\n");
    }
}

int main(void)
{
    (void)write_str("Rodnix userspace init launcher\n");
    print_file("/etc/motd");
    (void)write_str("\n");
    print_hostname();
    run_smoke();
    run_contract_mode_if_enabled();

    (void)write_str("[USER] init: exec /bin/sh\n");
    long ret = posix_exec("/bin/sh");
    if (ret < 0) {
        (void)write_str("[USER] init: exec failed\n");
        (void)posix_exit(1);
    }

    for (;;) {
        (void)rdnx_syscall0(0);
    }
    return 0;
}
