#include <stddef.h>
#include <stdint.h>
#include "unistd.h"

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

int main(void)
{
    int p[2] = {-1, -1};
    char buf[16];
    const char* msg = "pipe-ok";

    if (pipe(p) != 0) {
        (void)write_str("pipetest: pipe failed\n");
        return 1;
    }

    if (write(p[1], msg, 7) != 7) {
        (void)write_str("pipetest: write failed\n");
        (void)close(p[0]);
        (void)close(p[1]);
        return 1;
    }

    (void)close(p[1]);

    ssize_t n = read(p[0], buf, sizeof(buf));
    (void)close(p[0]);
    if (n != 7) {
        (void)write_str("pipetest: read failed\n");
        return 1;
    }

    if (buf[0] != 'p' || buf[1] != 'i' || buf[2] != 'p' || buf[3] != 'e' ||
        buf[4] != '-' || buf[5] != 'o' || buf[6] != 'k') {
        (void)write_str("pipetest: payload mismatch\n");
        return 1;
    }

    (void)write_str("pipetest: PASS\n");
    return 0;
}
