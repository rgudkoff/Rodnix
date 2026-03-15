/**
 * @file arch/apic.h
 * @brief Common entry point for local/io APIC helpers.
 */

#ifndef _RODNIX_ARCH_APIC_H
#define _RODNIX_ARCH_APIC_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/apic.h"
#else
#error "APIC interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_APIC_H */
