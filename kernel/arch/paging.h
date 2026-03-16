/**
 * @file arch/paging.h
 * @brief Common entry point for architecture paging helpers.
 */

#ifndef _RODNIX_ARCH_PAGING_H
#define _RODNIX_ARCH_PAGING_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/paging.h"
#else
#error "Architecture paging interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_PAGING_H */
