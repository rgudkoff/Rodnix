/**
 * @file arch/gdt.h
 * @brief Common entry point for architecture task state helpers.
 */

#ifndef _RODNIX_ARCH_GDT_H
#define _RODNIX_ARCH_GDT_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/gdt.h"
#else
#error "Architecture task-state interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_GDT_H */
