#ifndef _RODNIX_CONSOLE_H
#define _RODNIX_CONSOLE_H

#include "../include/types.h"

void console_init(void);
void kputc(char c);
void kputs(const char* s);
void kprint_hex(uint32_t v);
void kprint_dec(uint32_t v);

#endif

