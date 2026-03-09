#ifndef _RODNIX_ARCH_X86_64_USERMODE_H
#define _RODNIX_ARCH_X86_64_USERMODE_H

#include <stdint.h>

int usermode_prepare_stub(void** entry, void** user_stack, uint64_t* rsp0_out);
void usermode_enter(void* entry, void* user_stack, uint64_t rsp0, uint64_t arg0, uint64_t arg1, uint64_t arg2);
void usermode_set_pml4(uint64_t pml4_phys);

#endif /* _RODNIX_ARCH_X86_64_USERMODE_H */
