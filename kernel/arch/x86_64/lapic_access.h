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
bool lapic_access_ready(void);
lapic_mode_t lapic_access_mode(void);
const char* lapic_access_mode_name(void);

uint32_t lapic_access_read(uint32_t reg_off);
void lapic_access_write(uint32_t reg_off, uint32_t value);

#endif /* _RODNIX_ARCH_X86_64_LAPIC_ACCESS_H */
