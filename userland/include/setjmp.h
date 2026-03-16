#ifndef _RODNIX_USERLAND_SETJMP_H
#define _RODNIX_USERLAND_SETJMP_H

#include <stddef.h>

#if defined(__x86_64__) || defined(__x86_64) || defined(__RODNIX_X86_64_SETJMP__)
/*
 * x86_64 jmp_buf: 8 slots × 8 bytes = 64 bytes
 *   [0] rbx  [1] rbp  [2] r12  [3] r13
 *   [4] r14  [5] r15  [6] rsp  [7] rip
 */
typedef long jmp_buf[8];

#elif defined(__aarch64__)
/*
 * AArch64 jmp_buf: 22 slots × 8 bytes = 176 bytes
 *   x19-x28 (10), x29 (fp), x30 (lr), sp, d8-d15 (8 FP callee-saved)
 */
typedef long jmp_buf[22];

#elif defined(__riscv) && (__riscv_xlen == 64)
/*
 * RISC-V rv64 jmp_buf: 14 slots × 8 bytes = 112 bytes
 *   ra, s0-s11 (13), sp
 */
typedef long jmp_buf[14];

#else
#error "setjmp.h: unsupported architecture — add a jmp_buf definition"
#endif

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif /* _RODNIX_USERLAND_SETJMP_H */
