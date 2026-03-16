/**
 * @file arch/pmm.h
 * @brief Common entry point for architecture PMM helpers.
 */

#ifndef _RODNIX_ARCH_PMM_H
#define _RODNIX_ARCH_PMM_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/pmm.h"
#else
#error "Architecture PMM interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_PMM_H */
