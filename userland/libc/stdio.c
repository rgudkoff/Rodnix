#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    __SLBF = 0x0001,
    __SNBF = 0x0002,
    __SRD = 0x0004,
    __SWR = 0x0008,
    __SRW = 0x0010,
    __SEOF = 0x0020,
    __SERR = 0x0040,
    __SSTR = 0x0200,
    __SAPP = 0x0100,
    __SOWN = 0x4000
};

static int file_close_cookie(void* cookie);
static int file_read_cookie(void* cookie, char* buf, int n);
static fpos_t file_seek_cookie(void* cookie, fpos_t off, int whence);
static int file_write_cookie(void* cookie, const char* buf, int n);

static FILE g_stdin = {
    ._flags = __SRD,
    ._file = STDIN_FILENO,
    ._cookie = &g_stdin,
    ._close = file_close_cookie,
    ._read = file_read_cookie,
    ._seek = file_seek_cookie,
    ._write = file_write_cookie,
    ._ungot = 0,
    ._offset = 0,
};

static FILE g_stdout = {
    ._flags = __SWR,
    ._file = STDOUT_FILENO,
    ._cookie = &g_stdout,
    ._close = file_close_cookie,
    ._read = file_read_cookie,
    ._seek = file_seek_cookie,
    ._write = file_write_cookie,
    ._ungot = 0,
    ._offset = 0,
};

static FILE g_stderr = {
    ._flags = __SWR | __SNBF,
    ._file = STDERR_FILENO,
    ._cookie = &g_stderr,
    ._close = file_close_cookie,
    ._read = file_read_cookie,
    ._seek = file_seek_cookie,
    ._write = file_write_cookie,
    ._ungot = 0,
    ._offset = 0,
};

FILE* __stdinp = &g_stdin;
FILE* __stdoutp = &g_stdout;
FILE* __stderrp = &g_stderr;

static void buf_putc(char c, char* str, size_t size, size_t* idx, int* total)
{
    if (str && *idx + 1 < size) {
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

static char* g_vfprintf_buf;
static size_t g_vfprintf_cap;

static int parse_mode(const char* mode, int* open_flags, int* stdio_flags)
{
    int plus = 0;

    if (!mode || !mode[0] || !open_flags || !stdio_flags) {
        errno = EINVAL;
        return -1;
    }

    while (*mode) {
        if (*mode == '+') {
            plus = 1;
        } else if (*mode == 'b') {
            /* no-op */
        } else {
            break;
        }
        mode++;
    }

    switch (*mode) {
        case 'r':
            *open_flags = plus ? O_RDWR : O_RDONLY;
            *stdio_flags = plus ? (__SRW | __SRD | __SWR) : __SRD;
            break;
        case 'w':
            *open_flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
            *stdio_flags = plus ? (__SRW | __SRD | __SWR) : __SWR;
            break;
        case 'a':
            *open_flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
            *stdio_flags = (plus ? (__SRW | __SRD | __SWR) : __SWR) | __SAPP;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    return 0;
}

static FILE* file_alloc_from_fd(int fd, int stdio_flags, int owned)
{
    FILE* fp;

    if (fd < 0) {
        errno = EBADF;
        return NULL;
    }

    fp = (FILE*)malloc(sizeof(*fp));
    if (!fp) {
        errno = ENOMEM;
        return NULL;
    }

    memset(fp, 0, sizeof(*fp));
    fp->_flags = (short)(stdio_flags | (owned ? __SOWN : 0));
    fp->_file = (short)fd;
    fp->_cookie = fp;
    fp->_close = file_close_cookie;
    fp->_read = file_read_cookie;
    fp->_seek = file_seek_cookie;
    fp->_write = file_write_cookie;
    fp->_offset = 0;
    return fp;
}

static int stream_can_read(FILE* stream)
{
    return stream && ((stream->_flags & __SRD) || (stream->_flags & __SRW));
}

static int stream_can_write(FILE* stream)
{
    return stream && ((stream->_flags & __SWR) || (stream->_flags & __SRW));
}

static int file_close_cookie(void* cookie)
{
    FILE* fp = (FILE*)cookie;
    if (!fp || fp->_file < 0) {
        errno = EBADF;
        return -1;
    }
    return close(fp->_file);
}

static int file_read_cookie(void* cookie, char* buf, int n)
{
    FILE* fp = (FILE*)cookie;
    ssize_t r;

    if (!fp || !buf || n < 0) {
        errno = EINVAL;
        return -1;
    }
    r = read(fp->_file, buf, (size_t)n);
    if (r < 0) {
        fp->_flags |= __SERR;
        fp->_err = errno;
        return -1;
    }
    if (r == 0) {
        fp->_flags |= __SEOF;
    } else {
        fp->_flags &= (short)~__SEOF;
        fp->_offset += (fpos_t)r;
    }
    return (int)r;
}

static fpos_t file_seek_cookie(void* cookie, fpos_t off, int whence)
{
    FILE* fp = (FILE*)cookie;
    off_t out;

    if (!fp) {
        errno = EINVAL;
        return (fpos_t)-1;
    }
    out = lseek(fp->_file, (off_t)off, whence);
    if (out < 0) {
        fp->_flags |= __SERR;
        fp->_err = errno;
        return (fpos_t)-1;
    }
    fp->_flags &= (short)~__SEOF;
    fp->_offset = (fpos_t)out;
    fp->_ungot = 0;
    return (fpos_t)out;
}

static int file_write_cookie(void* cookie, const char* buf, int n)
{
    FILE* fp = (FILE*)cookie;
    ssize_t w;

    if (!fp || (!buf && n != 0) || n < 0) {
        errno = EINVAL;
        return -1;
    }
    w = write(fp->_file, buf, (size_t)n);
    if (w < 0) {
        fp->_flags |= __SERR;
        fp->_err = errno;
        return -1;
    }
    fp->_flags &= (short)~__SEOF;
    fp->_offset += (fpos_t)w;
    return (int)w;
}

int vsnprintf(char* str, size_t size, const char* fmt, va_list ap)
{
    size_t idx = 0;
    int total = 0;

    if (!fmt) {
        errno = EINVAL;
        return -1;
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

        switch (fmt[i]) {
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
                    buf_putu(va_arg(ap, unsigned long long), 16, fmt[i] == 'X', str, size, &idx, &total);
                } else if (long_flag) {
                    buf_putu(va_arg(ap, unsigned long), 16, fmt[i] == 'X', str, size, &idx, &total);
                } else {
                    buf_putu(va_arg(ap, unsigned int), 16, fmt[i] == 'X', str, size, &idx, &total);
                }
                break;
            case 'p':
                buf_puts("0x", str, size, &idx, &total);
                buf_putu((uintptr_t)va_arg(ap, void*), 16, 0, str, size, &idx, &total);
                break;
            default:
                buf_putc('%', str, size, &idx, &total);
                buf_putc(fmt[i], str, size, &idx, &total);
                break;
        }
    }

    if (str && size > 0) {
        if (idx < size) {
            str[idx] = '\0';
        } else {
            str[size - 1] = '\0';
        }
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

int sprintf(char* str, const char* fmt, ...)
{
    int out;
    va_list ap;

    va_start(ap, fmt);
    out = vsnprintf(str, (size_t)-1, fmt, ap);
    va_end(ap);
    return out;
}

int clearerr(FILE* stream)
{
    if (!stream) {
        errno = EBADF;
        return -1;
    }
    stream->_flags &= (short)~(__SEOF | __SERR);
    stream->_err = 0;
    return 0;
}

int fclose(FILE* stream)
{
    int rc = 0;

    if (!stream) {
        errno = EBADF;
        return EOF;
    }
    if (stream->_close && (stream->_flags & __SOWN)) {
        rc = stream->_close(stream->_cookie);
    }
    if (stream == stdin || stream == stdout || stream == stderr) {
        stream->_flags = 0;
        stream->_file = -1;
        return (rc == 0) ? 0 : EOF;
    }
    free(stream);
    return (rc == 0) ? 0 : EOF;
}

int feof(FILE* stream)
{
    if (!stream) {
        errno = EBADF;
        return 0;
    }
    return (stream->_flags & __SEOF) != 0;
}

int ferror(FILE* stream)
{
    if (!stream) {
        errno = EBADF;
        return 0;
    }
    return (stream->_flags & __SERR) != 0;
}

int fflush(FILE* stream)
{
    if (!stream) {
        return 0;
    }
    return 0;
}

int fgetc(FILE* stream)
{
    unsigned char ch;
    int rc;

    if (!stream || !stream_can_read(stream)) {
        errno = EBADF;
        return EOF;
    }
    if (stream->_ungot) {
        stream->_ungot = 0;
        return (int)stream->_ungotc;
    }
    rc = file_read_cookie(stream, (char*)&ch, 1);
    if (rc <= 0) {
        return EOF;
    }
    return (int)ch;
}

int fgetpos(FILE* stream, fpos_t* pos)
{
    long cur;

    if (!stream || !pos) {
        errno = EINVAL;
        return -1;
    }
    cur = ftell(stream);
    if (cur < 0) {
        return -1;
    }
    *pos = (fpos_t)cur;
    return 0;
}

char* fgets(char* s, int size, FILE* stream)
{
    int i;

    if (!s || size <= 0 || !stream) {
        errno = EINVAL;
        return NULL;
    }

    for (i = 0; i + 1 < size; i++) {
        int ch = fgetc(stream);
        if (ch == EOF) {
            break;
        }
        s[i] = (char)ch;
        if (ch == '\n') {
            i++;
            break;
        }
    }

    if (i == 0) {
        return NULL;
    }
    s[i] = '\0';
    return s;
}

FILE* fopen(const char* path, const char* mode)
{
    int open_flags;
    int stdio_flags;
    int fd;

    if (parse_mode(mode, &open_flags, &stdio_flags) != 0) {
        return NULL;
    }

    fd = open(path, open_flags);
    if (fd < 0) {
        return NULL;
    }
    return file_alloc_from_fd(fd, stdio_flags, 1);
}

int fprintf(FILE* stream, const char* fmt, ...)
{
    int out;
    va_list ap;

    va_start(ap, fmt);
    out = vfprintf(stream, fmt, ap);
    va_end(ap);
    return out;
}

int fputc(int c, FILE* stream)
{
    unsigned char ch = (unsigned char)c;

    if (!stream || !stream_can_write(stream)) {
        errno = EBADF;
        return EOF;
    }
    if (file_write_cookie(stream, (const char*)&ch, 1) != 1) {
        return EOF;
    }
    return (int)ch;
}

int fputs(const char* s, FILE* stream)
{
    size_t len = 0;

    if (!s || !stream || !stream_can_write(stream)) {
        errno = EINVAL;
        return EOF;
    }
    while (s[len] != '\0') {
        len++;
    }
    if (file_write_cookie(stream, s, (int)len) < 0) {
        return EOF;
    }
    return 0;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    size_t total;
    size_t done = 0;
    unsigned char* out = (unsigned char*)ptr;

    if (!ptr || !stream || size == 0 || nmemb == 0) {
        return 0;
    }
    total = size * nmemb;
    while (done < total) {
        int ch = fgetc(stream);
        if (ch == EOF) {
            break;
        }
        out[done++] = (unsigned char)ch;
    }
    return done / size;
}

FILE* freopen(const char* path, const char* mode, FILE* stream)
{
    int open_flags;
    int stdio_flags;
    int fd;

    if (!stream) {
        errno = EBADF;
        return NULL;
    }
    if (parse_mode(mode, &open_flags, &stdio_flags) != 0) {
        return NULL;
    }
    if (stream->_file >= 0) {
        (void)close(stream->_file);
    }
    fd = open(path, open_flags);
    if (fd < 0) {
        return NULL;
    }
    stream->_flags = (short)stdio_flags;
    stream->_file = (short)fd;
    stream->_ungot = 0;
    stream->_err = 0;
    stream->_offset = 0;
    stream->_cookie = stream;
    stream->_close = file_close_cookie;
    stream->_read = file_read_cookie;
    stream->_seek = file_seek_cookie;
    stream->_write = file_write_cookie;
    return stream;
}

int fseek(FILE* stream, long offset, int whence)
{
    if (!stream) {
        errno = EBADF;
        return -1;
    }
    return (stream->_seek(stream->_cookie, (fpos_t)offset, whence) < 0) ? -1 : 0;
}

int fsetpos(FILE* stream, const fpos_t* pos)
{
    if (!stream || !pos) {
        errno = EINVAL;
        return -1;
    }
    return fseek(stream, (long)*pos, SEEK_SET);
}

long ftell(FILE* stream)
{
    off_t out;

    if (!stream) {
        errno = EBADF;
        return -1L;
    }
    out = lseek(stream->_file, 0, SEEK_CUR);
    if (out < 0) {
        stream->_flags |= __SERR;
        stream->_err = errno;
        return -1L;
    }
    stream->_offset = (fpos_t)out;
    return (long)out;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    size_t total;
    int rc;

    if (!ptr || !stream || size == 0 || nmemb == 0) {
        return 0;
    }
    total = size * nmemb;
    rc = file_write_cookie(stream, (const char*)ptr, (int)total);
    if (rc < 0) {
        return 0;
    }
    return (size_t)rc / size;
}

int getc(FILE* stream)
{
    return fgetc(stream);
}

int getchar(void)
{
    return fgetc(stdin);
}

int perror(const char* s)
{
    const char* msg = strerror(errno);

    if (s && s[0] != '\0') {
        if (fputs(s, stderr) == EOF || fputs(": ", stderr) == EOF) {
            return EOF;
        }
    }
    if (fputs(msg, stderr) == EOF || fputc('\n', stderr) == EOF) {
        return EOF;
    }
    return 0;
}

int putc(int c, FILE* stream)
{
    return fputc(c, stream);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int printf(const char* fmt, ...)
{
    int n;
    va_list ap;

    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char* s)
{
    if (fputs(s, stdout) == EOF) {
        return EOF;
    }
    return (fputc('\n', stdout) == EOF) ? EOF : 0;
}

void rewind(FILE* stream)
{
    if (!stream) {
        return;
    }
    (void)fseek(stream, 0, SEEK_SET);
    (void)clearerr(stream);
}

int setvbuf(FILE* stream, char* buf, int mode, size_t size)
{
    (void)buf;
    (void)size;

    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    stream->_flags &= (short)~(__SLBF | __SNBF);
    if (mode == _IOLBF) {
        stream->_flags |= __SLBF;
    } else if (mode == _IONBF) {
        stream->_flags |= __SNBF;
    } else if (mode != _IOFBF) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void setbuf(FILE* stream, char* buf)
{
    (void)setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

int ungetc(int c, FILE* stream)
{
    if (!stream || c == EOF) {
        errno = EINVAL;
        return EOF;
    }
    if (stream->_ungot) {
        errno = ENOSYS;
        return EOF;
    }
    stream->_ungot = 1;
    stream->_ungotc = (unsigned char)c;
    stream->_flags &= (short)~__SEOF;
    return (unsigned char)c;
}

int vfprintf(FILE* stream, const char* fmt, va_list ap)
{
    va_list ap2;
    int n;

    if (!stream || !fmt || !stream_can_write(stream)) {
        errno = EINVAL;
        return EOF;
    }

    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) {
        return EOF;
    }

    if ((size_t)n + 1 > g_vfprintf_cap) {
        size_t want = (size_t)n + 1;
        char* new_buf = (char*)malloc(want);
        if (!new_buf) {
            errno = ENOMEM;
            return EOF;
        }
        g_vfprintf_buf = new_buf;
        g_vfprintf_cap = want;
    }

    if (vsnprintf(g_vfprintf_buf, g_vfprintf_cap, fmt, ap) < 0) {
        return EOF;
    }
    if (n > 0 && file_write_cookie(stream, g_vfprintf_buf, n) != n) {
        return EOF;
    }
    return n;
}

int vprintf(const char* fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

FILE* fdopen(int fd, const char* mode)
{
    int open_flags;
    int stdio_flags;

    if (parse_mode(mode, &open_flags, &stdio_flags) != 0) {
        return NULL;
    }
    (void)open_flags;
    return file_alloc_from_fd(fd, stdio_flags, 0);
}

int fileno(FILE* stream)
{
    if (!stream) {
        errno = EBADF;
        return -1;
    }
    return stream->_file;
}
