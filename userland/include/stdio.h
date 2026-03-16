#ifndef _RODNIX_USERLAND_STDIO_H
#define _RODNIX_USERLAND_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)

int putchar(int c);
int puts(const char* s);
int vsnprintf(char* str, size_t size, const char* fmt, va_list ap);
int snprintf(char* str, size_t size, const char* fmt, ...);
int vprintf(const char* fmt, va_list ap);
int printf(const char* fmt, ...);

#endif /* _RODNIX_USERLAND_STDIO_H */
