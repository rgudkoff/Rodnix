#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static void buf_putc(char c, char* str, size_t size, size_t* idx, int* total)
{
    if (*idx + 1 < size) {
        str[*idx] = c;
    }
    (*idx)++;
    (*total)++;
}

static void buf_puts(const char* s, char* str, size_t size, size_t* idx, int* total)
{
    if (!s) {
        s = "(null)";
    }
    while (*s) {
        buf_putc(*s++, str, size, idx, total);
    }
}

static void buf_putu(uint64_t v, unsigned base, int upper, char* str, size_t size, size_t* idx, int* total)
{
    char tmp[32];
    int i = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (base < 2 || base > 16) {
        return;
    }
    if (v == 0) {
        buf_putc('0', str, size, idx, total);
        return;
    }
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = digits[v % base];
        v /= base;
    }
    while (i-- > 0) {
        buf_putc(tmp[i], str, size, idx, total);
    }
}

static void buf_puti(int64_t v, char* str, size_t size, size_t* idx, int* total)
{
    if (v < 0) {
        buf_putc('-', str, size, idx, total);
        buf_putu((uint64_t)(-v), 10, 0, str, size, idx, total);
        return;
    }
    buf_putu((uint64_t)v, 10, 0, str, size, idx, total);
}

int vsnprintf(char* str, size_t size, const char* fmt, va_list ap)
{
    size_t idx = 0;
    int total = 0;
    if (!str || !fmt || size == 0) {
        return 0;
    }

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            buf_putc(fmt[i], str, size, &idx, &total);
            continue;
        }
        i++;
        if (fmt[i] == '\0') {
            break;
        }

        int long_flag = 0;
        int long_long_flag = 0;
        while (fmt[i] == 'l') {
            if (long_flag) {
                long_long_flag = 1;
                break;
            }
            long_flag = 1;
            i++;
        }
        char spec = fmt[i];
        switch (spec) {
            case '%':
                buf_putc('%', str, size, &idx, &total);
                break;
            case 'c':
                buf_putc((char)va_arg(ap, int), str, size, &idx, &total);
                break;
            case 's':
                buf_puts(va_arg(ap, const char*), str, size, &idx, &total);
                break;
            case 'd':
            case 'i':
                if (long_long_flag) {
                    buf_puti(va_arg(ap, long long), str, size, &idx, &total);
                } else if (long_flag) {
                    buf_puti(va_arg(ap, long), str, size, &idx, &total);
                } else {
                    buf_puti(va_arg(ap, int), str, size, &idx, &total);
                }
                break;
            case 'u':
                if (long_long_flag) {
                    buf_putu(va_arg(ap, unsigned long long), 10, 0, str, size, &idx, &total);
                } else if (long_flag) {
                    buf_putu(va_arg(ap, unsigned long), 10, 0, str, size, &idx, &total);
                } else {
                    buf_putu(va_arg(ap, unsigned int), 10, 0, str, size, &idx, &total);
                }
                break;
            case 'x':
            case 'X':
                if (long_long_flag) {
                    buf_putu(va_arg(ap, unsigned long long), 16, spec == 'X', str, size, &idx, &total);
                } else if (long_flag) {
                    buf_putu(va_arg(ap, unsigned long), 16, spec == 'X', str, size, &idx, &total);
                } else {
                    buf_putu(va_arg(ap, unsigned int), 16, spec == 'X', str, size, &idx, &total);
                }
                break;
            case 'p':
                buf_puts("0x", str, size, &idx, &total);
                buf_putu((uintptr_t)va_arg(ap, void*), 16, 0, str, size, &idx, &total);
                break;
            default:
                buf_putc('%', str, size, &idx, &total);
                buf_putc(spec, str, size, &idx, &total);
                break;
        }
    }

    if (idx < size) {
        str[idx] = '\0';
    } else {
        str[size - 1] = '\0';
    }
    return total;
}

int snprintf(char* str, size_t size, const char* fmt, ...)
{
    int out;
    va_list ap;
    va_start(ap, fmt);
    out = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return out;
}

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

int vprintf(const char* fmt, va_list ap)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n <= 0) {
        return n;
    }
    if (write(STDOUT_FILENO, buf, (size_t)n) < 0) {
        return EOF;
    }
    return n;
}

int printf(const char* fmt, ...)
{
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
