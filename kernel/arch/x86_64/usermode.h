#ifndef _RODNIX_ARCH_X86_64_USERMODE_H
#define _RODNIX_ARCH_X86_64_USERMODE_H

#include <stdint.h>

int usermode_prepare_stub(void** entry, void** user_stack, uint64_t* rsp0_out);
void usermode_enter(void* entry, void* user_stack, uint64_t rsp0);

#endif /* _RODNIX_ARCH_X86_64_USERMODE_H */
