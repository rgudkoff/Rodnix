/**
 * @file pmap_x86.h
 * @brief What this architecture's own code may know about a pmap.
 *
 * Exactly one thing: the page table root, for the two places that must load
 * CR3 themselves. Kept out of mm/pmap.h so that the machine-independent layer
 * has no way to reach it, which is the whole point of the type being opaque.
 */

#ifndef _RODNIX_ARCH_X86_64_PMAP_X86_H
#define _RODNIX_ARCH_X86_64_PMAP_X86_H

#include "../../../mm/pmap.h"

uint64_t pmap_x86_root(pmap_t pmap);

#endif /* _RODNIX_ARCH_X86_64_PMAP_X86_H */
