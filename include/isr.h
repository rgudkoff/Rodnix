#ifndef _RODNIX_ISR_H
#define _RODNIX_ISR_H

#include "types.h"

typedef struct registers
{
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

typedef void (*isr_handler_t)(registers_t* regs);

void isr_init(void);
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

#endif

