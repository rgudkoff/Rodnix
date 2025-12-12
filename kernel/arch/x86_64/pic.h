/**
 * @file pic.h
 * @brief PIC (Programmable Interrupt Controller) interface
 */

#ifndef _RODNIX_ARCH_X86_64_PIC_H
#define _RODNIX_ARCH_X86_64_PIC_H

#include <stdint.h>

/* Initialize PIC */
void pic_init(void);

/* Send EOI */
void pic_send_eoi(uint8_t irq);

/* Disable PIC */
void pic_disable(void);

/* Enable specific IRQ */
void pic_enable_irq(uint8_t irq);

/* Disable specific IRQ */
void pic_disable_irq(uint8_t irq);

#endif /* _RODNIX_ARCH_X86_64_PIC_H */

