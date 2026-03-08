#include <stdio.h>
#include <unistd.h>

int putchar(int c)
{
    char ch = (char)c;
    if (write(STDOUT_FILENO, &ch, 1) != 1) {
        return EOF;
    }
    return (unsigned char)ch;
}

int puts(const char* s)
{
    const char nl = '\n';
    size_t n = 0;
    if (!s) {
        return EOF;
    }
    while (s[n] != '\0') {
        n++;
    }
    if (write(STDOUT_FILENO, s, n) < 0) {
        return EOF;
    }
    if (write(STDOUT_FILENO, &nl, 1) < 0) {
        return EOF;
    }
    return 0;
}
