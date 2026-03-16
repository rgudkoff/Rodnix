/**
 * @file arch/pic.h
 * @brief Common entry point for legacy interrupt controller helpers.
 */

#ifndef _RODNIX_ARCH_PIC_H
#define _RODNIX_ARCH_PIC_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/pic.h"
#else
#error "Legacy PIC interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_PIC_H */
