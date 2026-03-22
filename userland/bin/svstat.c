#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(void)
{
    char buf[1024];
    int fd;
    ssize_t n;

    fd = open("/run/services.status", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "svstat: cannot open /run/services.status: %s\n", strerror(errno));
        return 1;
    }

    for (;;) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            fprintf(stderr, "svstat: read failed: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
        if (n == 0) {
            break;
        }
        if (write(STDOUT_FILENO, buf, (size_t)n) != n) {
            fprintf(stderr, "svstat: write failed\n");
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}
