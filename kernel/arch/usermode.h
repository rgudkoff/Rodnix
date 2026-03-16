/**
 * @file arch/usermode.h
 * @brief Common entry point for architecture usermode helpers.
 */

#ifndef _RODNIX_ARCH_USERMODE_H
#define _RODNIX_ARCH_USERMODE_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/usermode.h"
#else
#error "Architecture usermode interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_USERMODE_H */
