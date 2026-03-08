#include <ctype.h>
#include <stdlib.h>

int atoi(const char* nptr)
{
    int sign = 1;
    long v = 0;
    const char* s = nptr;

    if (!s) {
        return 0;
    }

    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '+' || *s == '-') {
        if (*s == '-') {
            sign = -1;
        }
        s++;
    }
    while (isdigit((unsigned char)*s)) {
        v = (v * 10) + (*s - '0');
        s++;
    }
    return (int)(sign * v);
}
