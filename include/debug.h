#ifndef _RODNIX_DEBUG_H
#define _RODNIX_DEBUG_H

#include "isr.h"

void panic(const char* msg, registers_t* r);
void dump_regs(registers_t* r);

#endif

