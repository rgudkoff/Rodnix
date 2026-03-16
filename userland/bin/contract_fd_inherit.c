/*
 * contract_fd_inherit.c
 * Probe binary for FD inheritance and close-on-exec checks.
 *
 * Usage:
 *   /bin/contract_fd_inherit [fd]
 * If fd is omitted, probe defaults to fd=3.
 * Exit status:
 *   0  -> read(fd,1) succeeded
 *   5  -> argv[1] parse failed
 *   2  -> read failed (fd unavailable in child image)
 */

#include <unistd.h>

static int parse_fd(const char* s)
{
    int v = 0;
    int i = 0;
    if (!s || s[0] == '\0') {
        return -1;
    }
    while (s[i] != '\0') {
        char c = s[i++];
        if (c < '0' || c > '9') {
            return -1;
        }
        v = v * 10 + (int)(c - '0');
        if (v > 1024) {
            return -1;
        }
    }
    return v;
}

int main(int argc, char** argv)
{
    char b = 0;
    int fd = 3;
    if (argc >= 2 && argv[1]) {
        fd = parse_fd(argv[1]);
        if (fd < 0) {
            return 5;
        }
    }
    return (read(fd, &b, 1) >= 0) ? 0 : 2;
}
