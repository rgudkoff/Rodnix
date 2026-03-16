#ifndef _RODNIX_USERLAND_STDLIB_H
#define _RODNIX_USERLAND_STDLIB_H

#include <stddef.h>

int atoi(const char* nptr);
long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);

void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t nmemb, size_t size);

#endif /* _RODNIX_USERLAND_STDLIB_H */
