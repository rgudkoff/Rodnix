#include "common.h"

#include "posix_syscall.h"
#include "unistd.h"

#define FD_STDOUT 1
#define VFS_OPEN_READ 1

long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

void write_hex_byte(uint8_t b)
{
    char hex[2];
    static const char table[] = "0123456789ABCDEF";
    hex[0] = table[(b >> 4) & 0xF];
    hex[1] = table[b & 0xF];
    (void)write_buf(hex, 2);
}

void copy_str(char* dst, size_t cap, const char* src)
{
    size_t i = 0;

    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int cstr_eq(const char* a, const char* b)
{
    uint64_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

int cstr_contains(const char* s, const char* needle)
{
    uint64_t i = 0;
    uint64_t j = 0;
    if (!s || !needle || !needle[0]) {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        for (j = 0; needle[j] && s[i + j] == needle[j]; j++) {
            ;
        }
        if (!needle[j]) {
            return 1;
        }
    }
    return 0;
}

int file_exists(const char* path)
{
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return 0;
    }
    (void)posix_close((int)fd);
    return 1;
}
