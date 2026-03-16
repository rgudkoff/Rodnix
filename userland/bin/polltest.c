#include <stdint.h>
#include <unistd.h>
#include <sys/poll.h>

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
    struct pollfd fds[1];
    char c = 'X';

    if (pipe(p) != 0) {
        (void)write_str("polltest: pipe failed\n");
        return 1;
    }

    fds[0].fd = p[0];
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    if (poll(fds, 1, 0) != 0) {
        (void)write_str("polltest: empty pipe should timeout\n");
        return 1;
    }

    if (write(p[1], "Q", 1) != 1) {
        (void)write_str("polltest: write failed\n");
        return 1;
    }

    fds[0].revents = 0;
    if (poll(fds, 1, 0) != 1 || (fds[0].revents & POLLIN) == 0) {
        (void)write_str("polltest: poll did not report POLLIN\n");
        return 1;
    }

    if (read(p[0], &c, 1) != 1 || c != 'Q') {
        (void)write_str("polltest: readback failed\n");
        return 1;
    }

    (void)close(p[1]);
    fds[0].revents = 0;
    if (poll(fds, 1, 0) != 1 || (fds[0].revents & POLLHUP) == 0) {
        (void)write_str("polltest: expected POLLHUP after writer close\n");
        return 1;
    }

    (void)close(p[0]);

    fds[0].fd = -1;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    if (poll(fds, 1, 0) != 1 || (fds[0].revents & POLLNVAL) == 0) {
        (void)write_str("polltest: expected POLLNVAL for bad fd\n");
        return 1;
    }

    (void)write_str("polltest: PASS\n");
    return 0;
}
