/*
 * bootstrap.c
 * Minimal userland init stub.
 */

#include "syscall.h"
#include "posix_syscall.h"

#define SYS_NOP 0

static long posix_write_str(const char* s)
{
    long len = 0;
    while (s[len]) {
        len++;
    }
    return posix_write(1, s, (uint64_t)len);
}

int main(void)
{
    posix_write_str("[USER] init: hello from ring3 (posix write)\n");
    posix_write_str("[USER] init: press a key to test stdin...\n");
    char ch = 0;
    if (posix_read(0, &ch, 1) == 1) {
        char out[32];
        out[0] = '['; out[1] = 'U'; out[2] = 'S'; out[3] = 'E'; out[4] = 'R';
        out[5] = ']'; out[6] = ' '; out[7] = 'i'; out[8] = 'n'; out[9] = 'i';
        out[10] = 't'; out[11] = ':'; out[12] = ' '; out[13] = 'g'; out[14] = 'o';
        out[15] = 't'; out[16] = ' '; out[17] = '\''; out[18] = ch; out[19] = '\'';
        out[20] = '\n'; out[21] = '\0';
        posix_write(1, out, 21);
    }
    posix_exit(0);
    for (;;) {
        (void)rdnx_syscall0(SYS_NOP);
    }
    return 0;
}
