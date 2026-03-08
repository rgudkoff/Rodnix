#ifndef _RODNIX_X86_64_SYSCALL_FAST_H
#define _RODNIX_X86_64_SYSCALL_FAST_H

#include <stdint.h>

typedef struct interrupt_frame interrupt_frame_t;

int x86_64_syscall_fast_init(void);
uint64_t x86_64_syscall_dispatch_frame(interrupt_frame_t* frame, int fast_entry);
uint64_t x86_64_syscall_fast_dispatch_frame(interrupt_frame_t* frame);

#endif /* _RODNIX_X86_64_SYSCALL_FAST_H */
