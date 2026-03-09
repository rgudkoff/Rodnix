#ifndef _RODNIX_USERLAND_STDIO_H
#define _RODNIX_USERLAND_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>

#define EOF (-1)

#define BUFSIZ 1024

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

typedef off_t fpos_t;

struct __sbuf {
    unsigned char* _base;
    int _size;
};

typedef struct __sFILE {
    unsigned char* _p;
    int _r;
    int _w;
    short _flags;
    short _file;
    struct __sbuf _bf;
    int _lbfsize;

    void* _cookie;
    int (*_close)(void*);
    int (*_read)(void*, char*, int);
    fpos_t (*_seek)(void*, fpos_t, int);
    int (*_write)(void*, const char*, int);

    int _err;
    int _ungot;
    unsigned char _ungotc;
    fpos_t _offset;
} FILE;

extern FILE* __stdinp;
extern FILE* __stdoutp;
extern FILE* __stderrp;

#define stdin  __stdinp
#define stdout __stdoutp
#define stderr __stderrp

int clearerr(FILE* stream);
int fclose(FILE* stream);
int feof(FILE* stream);
int ferror(FILE* stream);
int fflush(FILE* stream);
int fgetc(FILE* stream);
int fgetpos(FILE* stream, fpos_t* pos);
char* fgets(char* s, int size, FILE* stream);
FILE* fopen(const char* path, const char* mode);
int fprintf(FILE* stream, const char* fmt, ...);
int fputc(int c, FILE* stream);
int fputs(const char* s, FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
FILE* freopen(const char* path, const char* mode, FILE* stream);
int fseek(FILE* stream, long offset, int whence);
int fsetpos(FILE* stream, const fpos_t* pos);
long ftell(FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int getc(FILE* stream);
int getchar(void);
int perror(const char* s);
int putc(int c, FILE* stream);
int putchar(int c);
int printf(const char* fmt, ...);
int puts(const char* s);
void rewind(FILE* stream);
int setvbuf(FILE* stream, char* buf, int mode, size_t size);
void setbuf(FILE* stream, char* buf);
int ungetc(int c, FILE* stream);
int snprintf(char* str, size_t size, const char* fmt, ...);
int sprintf(char* str, const char* fmt, ...);
int vfprintf(FILE* stream, const char* fmt, va_list ap);
int vprintf(const char* fmt, va_list ap);
int vsnprintf(char* str, size_t size, const char* fmt, va_list ap);
FILE* fdopen(int fd, const char* mode);
int fileno(FILE* stream);

#endif /* _RODNIX_USERLAND_STDIO_H */
