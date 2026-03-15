#include <stddef.h>
#include <errno.h>
#include <string.h>

size_t strlen(const char* s)
{
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int strcmp(const char* a, const char* b)
{
    unsigned char ca;
    unsigned char cb;
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    do {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
    } while (ca != '\0');
    return 0;
}

int strncmp(const char* a, const char* b, size_t n)
{
    if (n == 0 || a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (n-- > 0) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (ca == '\0') {
            return 0;
        }
    }
    return 0;
}

void* memset(void* dst, int c, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

void* memmove(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n)
{
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    while (n-- > 0) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}

char* strcpy(char* dst, const char* src)
{
    char* out = dst;
    if (!dst || !src) {
        return dst;
    }
    while ((*dst++ = *src++) != '\0') {
    }
    return out;
}

char* strncpy(char* dst, const char* src, size_t n)
{
    size_t i = 0;
    if (!dst || !src) {
        return dst;
    }
    for (; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = '\0';
    }
    return dst;
}

char* strchr(const char* s, int c)
{
    char ch = (char)c;
    if (!s) {
        return 0;
    }
    for (;; s++) {
        if (*s == ch) {
            return (char*)s;
        }
        if (*s == '\0') {
            return 0;
        }
    }
}

char* strerror(int errnum)
{
    switch (errnum) {
        case 0: return "No error";
        case EPERM: return "Operation not permitted";
        case ENOENT: return "No such file or directory";
        case ESRCH: return "No such process";
        case EINTR: return "Interrupted system call";
        case EIO: return "Input/output error";
        case ENXIO: return "Device not configured";
        case E2BIG: return "Argument list too long";
        case ENOEXEC: return "Exec format error";
        case EBADF: return "Bad file descriptor";
        case ECHILD: return "No child processes";
        case EDEADLK: return "Resource deadlock avoided";
        case ENOMEM: return "Cannot allocate memory";
        case EACCES: return "Permission denied";
        case EFAULT: return "Bad address";
        case EBUSY: return "Device or resource busy";
        case EEXIST: return "File exists";
        case ENODEV: return "Operation not supported by device";
        case ENOTDIR: return "Not a directory";
        case EISDIR: return "Is a directory";
        case EINVAL: return "Invalid argument";
        case ENFILE: return "Too many open files in system";
        case EMFILE: return "Too many open files";
        case ENOSPC: return "No space left on device";
        case ESPIPE: return "Illegal seek";
        case EROFS: return "Read-only file system";
        case EPIPE: return "Broken pipe";
        case ERANGE: return "Result too large";
        case EAGAIN: return "Resource temporarily unavailable";
        case ENOSYS: return "Function not implemented";
        default: return "Unknown error";
    }
}
