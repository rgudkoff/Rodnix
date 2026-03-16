#ifndef _RODNIX_USERLAND_STDLIB_H
#define _RODNIX_USERLAND_STDLIB_H

#include <stddef.h>

int atoi(const char* nptr);
double atof(const char* nptr);
long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);
unsigned long long strtoull(const char* nptr, char** endptr, int base);
double strtod(const char* nptr, char** endptr);
float strtof(const char* nptr, char** endptr);
long double strtold(const char* nptr, char** endptr);

void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t nmemb, size_t size);

int abs(int x);
long labs(long x);
long long llabs(long long x);

int atexit(void (*fn)(void));
void exit(int status);
void abort(void);

extern char** environ;
char* getenv(const char* name);

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*));

int system(const char* cmd);

#endif /* _RODNIX_USERLAND_STDLIB_H */
