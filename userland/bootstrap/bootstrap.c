/*
 * bootstrap.c
 * Minimal userland init stub.
 */

#include "syscall.h"
#include "posix_syscall.h"

#define SYS_NOP 0
#define VFS_OPEN_READ 1

static long posix_write_str(const char* s)
{
    long len = 0;
    while (s[len]) {
        len++;
    }
    return posix_write(1, s, (uint64_t)len);
}

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        posix_write(1, "0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        i--;
        posix_write(1, &buf[i], 1);
    }
}

static void write_hex_byte(uint8_t b)
{
    char hex[2];
    static const char table[] = "0123456789ABCDEF";
    hex[0] = table[(b >> 4) & 0xF];
    hex[1] = table[b & 0xF];
    posix_write(1, hex, 2);
}

int main(void)
{
    posix_write_str("[USER] init: POSIX smoke test start\n");

    posix_write_str("[USER] getpid=");
    {
        long pid = posix_getpid();
        if (pid < 0) {
            posix_write_str("ERR\n");
        } else {
            write_u64((uint64_t)pid);
            posix_write_str("\n");
        }
    }

    {
        long fd = posix_open("/bin/init", VFS_OPEN_READ);
        if (fd < 0) {
            posix_write_str("[USER] open('/bin/init') failed\n");
        } else {
            uint8_t hdr[4] = {0, 0, 0, 0};
            long n = posix_read((int)fd, hdr, sizeof(hdr));
            posix_write_str("[USER] read('/bin/init') bytes=");
            if (n < 0) {
                posix_write_str("ERR\n");
            } else {
                write_u64((uint64_t)n);
                posix_write_str(" hdr=");
                for (long i = 0; i < n; i++) {
                    write_hex_byte(hdr[i]);
                }
                posix_write_str("\n");
            }
            if (posix_close((int)fd) < 0) {
                posix_write_str("[USER] close failed\n");
            } else {
                posix_write_str("[USER] close ok\n");
            }
        }
    }

    posix_write_str("[USER] init: POSIX smoke test done\n");
    (void)posix_exit(0);
    for (;;) {
        (void)rdnx_syscall0(SYS_NOP);
    }
    return 0;
}
