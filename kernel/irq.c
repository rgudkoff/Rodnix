#include "irq.h"
#include "pic.h"
#include "isr.h"

void irq_init(void)
{
    pic_remap(0x20, 0x28);
}

void irq_ack(uint8_t irq)
{
    pic_send_eoi(irq);
}

