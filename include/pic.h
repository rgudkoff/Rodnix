#ifndef _RODNIX_PIC_H
#define _RODNIX_PIC_H

#include "types.h"

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_send_eoi(uint8_t irq);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);

#endif
