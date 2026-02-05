#ifndef _RODNIX_ARCH_X86_64_GDT_H
#define _RODNIX_ARCH_X86_64_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10
#define GDT_USER_DS   0x18
#define GDT_USER_CS   0x20
#define GDT_TSS_SEL   0x28

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif /* _RODNIX_ARCH_X86_64_GDT_H */
