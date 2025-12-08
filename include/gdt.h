#ifndef _RODNIX_GDT_H
#define _RODNIX_GDT_H

#include "types.h"
#include "common.h"

struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} PACKED;

struct gdt_ptr
{
    uint16_t limit;
    uint32_t base;
} PACKED;

void gdt_init(void);

#endif

