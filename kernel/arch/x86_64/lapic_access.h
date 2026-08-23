#ifndef _RODNIX_ARCH_X86_64_LAPIC_ACCESS_H
#define _RODNIX_ARCH_X86_64_LAPIC_ACCESS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum lapic_mode {
    LAPIC_MODE_NONE = 0,
    LAPIC_MODE_XAPIC = 1,
    LAPIC_MODE_X2APIC = 2
} lapic_mode_t;

int lapic_access_init(uint64_t apic_phys, bool prefer_x2apic);
/* Enable the calling processor's own LAPIC in the mode the BSP already chose.
 * The MMIO mapping and the mode decision are global; the APIC_BASE enable bit
 * is per-CPU, so each AP has to set its own. */
int lapic_access_init_ap(void);

bool lapic_access_ready(void);
lapic_mode_t lapic_access_mode(void);
const char* lapic_access_mode_name(void);

uint32_t lapic_access_read(uint32_t reg_off);
void lapic_access_write(uint32_t reg_off, uint32_t value);

/* Write the ICR as one operation.
 *
 * Not expressible through lapic_access_write(): in xAPIC the ICR is a pair of
 * 32-bit registers that must be written high-then-low, and the low write is
 * what fires the IPI. In x2APIC there is no high register at all -- the whole
 * thing is a single 64-bit MSR -- so routing 0x310 through the generic mapping
 * would address MSR 0x831, which does not exist, and #GP.
 *
 * `dest` is a plain APIC ID; the shift the xAPIC layout wants is applied here. */
void lapic_access_write_icr(uint32_t dest, uint32_t low);

/* Spin until the previous IPI has left the ICR. Always true in x2APIC, where
 * the delivery-status bit is reserved. Returns false if `spins` ran out. */
bool lapic_access_ipi_wait(uint32_t spins);

#endif /* _RODNIX_ARCH_X86_64_LAPIC_ACCESS_H */
