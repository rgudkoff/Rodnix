/**
 * @file arch/cpu_topology.h
 * @brief Common entry point for the processor inventory.
 */

#ifndef _RODNIX_ARCH_CPU_TOPOLOGY_H
#define _RODNIX_ARCH_CPU_TOPOLOGY_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/cpu_topology.h"
#else
#error "Processor inventory is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_CPU_TOPOLOGY_H */
