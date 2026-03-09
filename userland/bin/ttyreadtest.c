/*
 * ttyreadtest.c
 * Single blocking stdin read probe for syscall/TTY diagnostics.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"

#define FD_STDIN 0
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

int main(void)
{
    char c = 0;
    (void)write_str("ttyreadtest: before read(0,1)\n");
    long rc = posix_read(FD_STDIN, &c, 1);
    (void)write_str("ttyreadtest: after read rc=");
    if (rc < 0) {
        write_u64((uint64_t)(-rc));
        (void)write_str(" (neg)\n");
        return 1;
    }
    write_u64((uint64_t)rc);
    (void)write_str(" byte=");
    if (rc > 0 && c >= 32 && c <= 126) {
        (void)write_buf(&c, 1);
    } else if (rc > 0) {
        (void)write_str("?");
    }
    (void)write_str("\n");
    return 0;
}
