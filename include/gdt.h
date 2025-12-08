#ifndef _RODNIX_GDT_H
#define _RODNIX_GDT_H

#include "types.h"

/* GDT Entry */
struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/* GDT Pointer */
struct gdt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void gdt_init(void);
void gdt_flush(void);

#endif
