#ifndef _RODNIX_IDT_H
#define _RODNIX_IDT_H

#include "types.h"

/* IDT Entry */
struct idt_entry
{
    uint16_t base_low;
    uint16_t selector;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

/* IDT Pointer */
struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);
void idt_load(void);

#endif
