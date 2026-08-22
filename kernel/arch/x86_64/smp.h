/**
 * @file smp.h
 * @brief Application processor bring-up.
 */

#ifndef _RODNIX_ARCH_X86_64_SMP_H
#define _RODNIX_ARCH_X86_64_SMP_H

#include <stdint.h>

/* Physical address the STARTUP IPI points application processors at.
 * Must be page-aligned and below 1MB: the SIPI vector is a single byte and
 * the processor begins executing at vector << 12. The PMM does not manage
 * memory below 1MB (PMM_MEMORY_START is 0x100000), so nothing else is
 * allocating here; the range is reserved anyway to say so out loud. */
#define AP_TRAMPOLINE_PHYS 0x8000UL

/* Bring up every processor the topology reports as startable, one at a time.
 * Returns the number that came online, not counting the boot processor.
 * Safe to call on a machine with one processor: it does nothing. */
int smp_start_aps(void);

/* Send every online AP an IPI and wait for it to answer. Returns the number
 * that replied, or -1 if any did not. Proves the processors are running, not
 * merely that they set a flag on their way past. */
int smp_verify_aps(void);

/* Processors that answered, including the boot processor. */
uint32_t smp_online_count(void);

#endif /* _RODNIX_ARCH_X86_64_SMP_H */
