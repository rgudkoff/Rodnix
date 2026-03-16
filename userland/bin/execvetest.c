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

static int str_eq(const char* a, const char* b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int str_starts(const char* s, const char* pfx)
{
    if (!s || !pfx) {
        return 0;
    }
    while (*pfx) {
        if (*s != *pfx) {
            return 0;
        }
        s++;
        pfx++;
    }
    return 1;
}

int main(int argc, char** argv, char** envp)
{
    if (argc >= 2 && argv && argv[1] && str_eq(argv[1], "--child")) {
        int env_ok = 0;
        for (int i = 0; envp && envp[i]; i++) {
            if (str_starts(envp[i], "RDNX_EXEC_ENV=") && str_eq(envp[i] + 14, "ok")) {
                env_ok = 1;
                break;
            }
        }
        if (!env_ok) {
            (void)write_str("execvetest: FAIL envp missing\n");
            return 2;
        }
        (void)write_str("execvetest: PASS argv+envp\n");
        return 0;
    }

    (void)write_str("execvetest: execve self (--child)\n");
    char* av[3];
    av[0] = (char*)"execvetest";
    av[1] = (char*)"--child";
    av[2] = 0;
    char* ev[3];
    ev[0] = (char*)"RDNX_EXEC_ENV=ok";
    ev[1] = (char*)"PATH=/bin";
    ev[2] = 0;
    int rc = execve("/bin/execvetest", av, ev);
    (void)write_str("execvetest: execve failed rc=");
    if (rc < 0) {
        (void)write_str("-1\n");
    } else {
        (void)write_str("0\n");
    }
    return 1;
}
