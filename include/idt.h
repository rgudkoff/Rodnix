#ifndef _RODNIX_IDT_H
#define _RODNIX_IDT_H

#include "types.h"
#include "common.h"

struct idt_entry
{
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} PACKED;

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} PACKED;

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
extern struct idt_entry idt[256];
extern struct idt_ptr   idtp;

#endif

