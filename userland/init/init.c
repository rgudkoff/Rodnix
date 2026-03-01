/*
 * init.c
 * Minimal userspace launcher: smoke + exec /bin/sh.
 */

#include <stdint.h>
#include "syscall.h"
#include "posix_syscall.h"

#define VFS_OPEN_READ 1
#define FD_STDOUT 1

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

int main(void)
{
    (void)write_str("Rodnix userspace init launcher\n");
    print_file("/etc/motd");
    (void)write_str("\n");
    print_hostname();
    run_smoke();

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
