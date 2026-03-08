/*
 * contract_fd.c
 * Contract checks for FD semantics.
 *
 * Spec traceability:
 * - CT-007 -> docs/ru/unix_process_model.md (close invalidates fd)
 * - CT-008 -> docs/ru/unix_process_model.md (read/write require valid fd)
 */

#include <stdint.h>
#include "posix_syscall.h"

#define FD_STDOUT 1
#define VFS_OPEN_READ 1

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

int main(void)
{
    int ok = 1;
    char b = 0;

    long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
    if (fd < 0) {
        (void)write_str("[CT] CT-007 FAIL open failed\n");
        ok = 0;
    } else {
        if (posix_close((int)fd) < 0) {
            (void)write_str("[CT] CT-007 FAIL close failed\n");
            ok = 0;
        } else {
            long r = posix_read((int)fd, &b, 1);
            if (r < 0) {
                (void)write_str("[CT] CT-007 PASS close invalidates fd\n");
            } else {
                (void)write_str("[CT] CT-007 FAIL read after close succeeded\n");
                ok = 0;
            }
        }
    }

    {
        long r = posix_read(-1, &b, 1);
        if (r < 0) {
            (void)write_str("[CT] CT-008 PASS read on invalid fd fails\n");
        } else {
            (void)write_str("[CT] CT-008 FAIL read on invalid fd succeeded\n");
            ok = 0;
        }
    }

    return ok ? 0 : 1;
}
