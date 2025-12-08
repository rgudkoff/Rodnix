#ifndef _RODNIX_IRQ_H
#define _RODNIX_IRQ_H

#include "types.h"
#include "isr.h"

void irq_init(void);
void irq_ack(uint8_t irq);
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);

#endif

