#include "idt.h"
#include "isr.h"

extern void idt_load(uint32_t);

struct idt_entry idt[256];
struct idt_ptr   idtp;

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;

    idt[num].sel     = sel;
    idt[num].always0 = 0;
    idt[num].flags   = flags;
}

void idt_init(void)
{
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base  = (uint32_t)&idt;

    for (int i = 0; i < 256; ++i)
        idt_set_gate((uint8_t)i, 0, 0x08, 0x8E);

    isr_init();

    idt_load((uint32_t)&idtp);
}

