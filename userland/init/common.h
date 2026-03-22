#ifndef _RODNIX_USERLAND_INIT_COMMON_H
#define _RODNIX_USERLAND_INIT_COMMON_H

#include <stdint.h>
#include <stddef.h>

long write_buf(const char* s, uint64_t len);
long write_str(const char* s);
void write_u64(uint64_t v);
void write_hex_byte(uint8_t b);
void copy_str(char* dst, size_t cap, const char* src);
int is_space_char(char c);
int cstr_eq(const char* a, const char* b);
int cstr_contains(const char* s, const char* needle);
int file_exists(const char* path);

#endif
