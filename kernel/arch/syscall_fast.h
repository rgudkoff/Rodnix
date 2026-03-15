/**
 * @file arch/syscall_fast.h
 * @brief Common entry point for fast syscall helpers.
 */

#ifndef _RODNIX_ARCH_SYSCALL_FAST_H
#define _RODNIX_ARCH_SYSCALL_FAST_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/syscall_fast.h"
#else
#error "Fast syscall interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_SYSCALL_FAST_H */
