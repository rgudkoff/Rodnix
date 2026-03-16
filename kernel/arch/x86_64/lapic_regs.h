#ifndef _RODNIX_ARCH_X86_64_LAPIC_REGS_H
#define _RODNIX_ARCH_X86_64_LAPIC_REGS_H

#include <stdint.h>

/* Local APIC Base Address MSR */
#define APIC_BASE_MSR        0x1B
#define APIC_BASE_ENABLE     (1ULL << 11)
#define APIC_BASE_BSP        (1ULL << 8)
#define APIC_BASE_X2APIC     (1ULL << 10)

/* x2APIC MSR base */
#define X2APIC_MSR_BASE      0x800

/* Local APIC register offsets */
#define APIC_ID              0x020
#define APIC_VERSION         0x030
#define APIC_TPR             0x080
#define APIC_APR             0x090
#define APIC_PPR             0x0A0
#define APIC_EOI             0x0B0
#define APIC_SVR             0x0F0
#define APIC_ESR             0x280
#define APIC_ICR_LOW         0x300
#define APIC_ICR_HIGH        0x310
#define APIC_LVT_TIMER       0x320
#define APIC_LVT_THERMAL     0x330
#define APIC_LVT_PERF        0x340
#define APIC_LVT_LINT0       0x350
#define APIC_LVT_LINT1       0x360
#define APIC_LVT_ERROR       0x370
#define APIC_TIMER_INITCNT   0x380
#define APIC_TIMER_CURRCNT   0x390
#define APIC_TIMER_DIV       0x3E0

/* APIC SVR flags */
#define APIC_SVR_ENABLE      (1U << 8)
#define APIC_SVR_SPURIOUS_VECTOR  0xFF

/* APIC LVT flags */
#define APIC_LVT_MASKED      (1U << 16)
#define APIC_LVT_TIMER_PERIODIC   (1U << 17)

#endif /* _RODNIX_ARCH_X86_64_LAPIC_REGS_H */
