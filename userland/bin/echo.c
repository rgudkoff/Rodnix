/*
 * echo.c
 * Minimal external userland program using POSIX-like headers.
 */

#include <unistd.h>

static unsigned long cstr_len(const char* s)
{
    unsigned long n = 0;
    if (!s) {
        return 0;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        const char* s = argv[i] ? argv[i] : "";
        (void)write(STDOUT_FILENO, s, cstr_len(s));
        if (i + 1 < argc) {
            (void)write(STDOUT_FILENO, " ", 1);
        }
    }
    (void)write(STDOUT_FILENO, "\n", 1);
    return 0;
}
