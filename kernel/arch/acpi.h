/**
 * @file arch/acpi.h
 * @brief Common entry point for platform firmware discovery helpers.
 */

#ifndef _RODNIX_ARCH_ACPI_H
#define _RODNIX_ARCH_ACPI_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/acpi.h"
#else
#error "Firmware discovery interface is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_ACPI_H */
