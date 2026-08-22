/**
 * @file arch/percpu.h
 * @brief Common entry point for per-CPU state.
 */

#ifndef _RODNIX_ARCH_PERCPU_H
#define _RODNIX_ARCH_PERCPU_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/percpu.h"
#else
#error "Per-CPU state is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_PERCPU_H */
