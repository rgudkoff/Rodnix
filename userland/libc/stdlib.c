#include <ctype.h>
#include <stdlib.h>

static int char_to_digit(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return -1;
}

int atoi(const char* nptr)
{
    return (int)strtol(nptr, 0, 10);
}

unsigned long strtoul(const char* nptr, char** endptr, int base)
{
    unsigned long value = 0;
    const char* s = nptr;
    int neg = 0;
    int any = 0;

    if (!s) {
        if (endptr) {
            *endptr = (char*)nptr;
        }
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s++;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    for (;;) {
        int d = char_to_digit((unsigned char)*s);
        if (d < 0 || d >= base) {
            break;
        }
        value = (value * (unsigned long)base) + (unsigned long)d;
        any = 1;
        s++;
    }
    if (endptr) {
        *endptr = (char*)(any ? s : nptr);
    }
    if (neg) {
        return (unsigned long)(-(long)value);
    }
    return value;
}

long strtol(const char* nptr, char** endptr, int base)
{
    return (long)strtoul(nptr, endptr, base);
}
