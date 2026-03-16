/*
 * execvetest.c
 * Simple execve(argv) probe.
 */

#include <stdint.h>
#include "unistd.h"

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
    (void)write_str("execvetest: execve /bin/echo ...\n");
    char* av[4];
    av[0] = (char*)"echo";
    av[1] = (char*)"execve";
    av[2] = (char*)"ok";
    av[3] = 0;
    char* envp[1];
    envp[0] = 0;
    int rc = execve("/bin/echo", av, envp);
    (void)write_str("execvetest: execve failed rc=");
    if (rc < 0) {
        (void)write_str("-1\n");
    } else {
        (void)write_str("0\n");
    }
    return 1;
}
