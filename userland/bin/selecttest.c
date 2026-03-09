#include <stdint.h>
#include <unistd.h>
#include <sys/select.h>

#define FD_STDOUT 1

static long write_buf(const char* s, uint64_t len)
{
    return write(FD_STDOUT, s, (size_t)len);
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
    fd_set rfds;
    struct timeval tv;
    char c = 'X';

    if (pipe(p) != 0) {
        (void)write_str("selecttest: pipe failed\n");
        return 1;
    }

    FD_ZERO(&rfds);
    FD_SET(p[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(p[0] + 1, &rfds, (fd_set*)0, (fd_set*)0, &tv) != 0) {
        (void)write_str("selecttest: empty pipe should timeout\n");
        return 1;
    }

    if (write(p[1], "S", 1) != 1) {
        (void)write_str("selecttest: write failed\n");
        return 1;
    }

    FD_ZERO(&rfds);
    FD_SET(p[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(p[0] + 1, &rfds, (fd_set*)0, (fd_set*)0, &tv) != 1 || !FD_ISSET(p[0], &rfds)) {
        (void)write_str("selecttest: expected readable fd\n");
        return 1;
    }

    if (read(p[0], &c, 1) != 1 || c != 'S') {
        (void)write_str("selecttest: readback failed\n");
        return 1;
    }

    (void)close(p[0]);
    (void)close(p[1]);
    (void)write_str("selecttest: PASS\n");
    return 0;
}
