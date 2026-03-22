#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main(void)
{
    struct winsize ws;

    if (!isatty(STDOUT_FILENO)) {
        fprintf(stderr, "ttysize: stdout is not a tty\n");
        return 1;
    }

    if (ioctl(STDOUT_FILENO, RDNX_TTY_IOCTL_GETWINSZ, &ws) != 0) {
        fprintf(stderr, "ttysize: ioctl(GETWINSZ) failed\n");
        return 1;
    }

    printf("%u %u\n", (unsigned)ws.ws_row, (unsigned)ws.ws_col);
    return 0;
}
