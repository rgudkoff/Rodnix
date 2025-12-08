#ifndef _RODNIX_ISR_H
#define _RODNIX_ISR_H

#include "types.h"

/* Регистры при прерывании */
/* Порядок соответствует порядку push в isr_stubs.S */
struct registers
{
    uint32_t ds;      /* push ds */
    uint32_t es;      /* push es */
    uint32_t fs;      /* push fs */
    uint32_t gs;      /* push gs */
    uint32_t edi;     /* pusha: edi */
    uint32_t esi;     /* pusha: esi */
    uint32_t ebp;     /* pusha: ebp */
    uint32_t esp_orig; /* pusha: esp (оригинальный, до pusha) */
    uint32_t ebx;     /* pusha: ebx */
    uint32_t edx;     /* pusha: edx */
    uint32_t ecx;     /* pusha: ecx */
    uint32_t eax;     /* pusha: eax */
    uint32_t int_no;  /* push dword interrupt_number */
    uint32_t err_code; /* push dword error_code (или 0) */
    uint32_t eip;     /* CPU автоматически */
    uint32_t cs;      /* CPU автоматически */
    uint32_t eflags;  /* CPU автоматически */
    uint32_t useresp; /* CPU автоматически (если переход из ring3) */
    uint32_t ss;      /* CPU автоматически (если переход из ring3) */
};

typedef void (*isr_handler_t)(struct registers*);

void isr_init(void);
void register_interrupt_handler(uint8_t n, isr_handler_t handler);

#endif
