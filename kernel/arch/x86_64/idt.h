/**
 * @file idt.h
 * @brief IDT (Interrupt Descriptor Table) interface
 * 
 * @note This interface follows XNU-style architecture for interrupt handling.
 */

#ifndef _RODNIX_ARCH_X86_64_IDT_H
#define _RODNIX_ARCH_X86_64_IDT_H

#include <stdint.h>

/* IDT entry type and attributes flags */
#define IDT_TYPE_INTERRUPT_GATE  0x8E  /* 64-bit interrupt gate (IF cleared on entry) */
#define IDT_TYPE_TRAP_GATE       0x8F  /* 64-bit trap gate (IF not cleared) */

/* Initialize IDT */
int idt_init(void);

/* Get handler address */
void* idt_get_handler(uint8_t vector);

/* Set custom handler (XNU-style interface) */
int idt_set_handler(uint8_t vector, void* handler, uint8_t type_attr, uint8_t ist);

#endif /* _RODNIX_ARCH_X86_64_IDT_H */

