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

/* x2APIC exposes the whole ICR as one 64-bit MSR rather than the xAPIC pair
 * at 0x300/0x310; there is no MSR for the high half. */
#define X2APIC_MSR_ICR       0x830

/* ICR (Interrupt Command Register) fields.
 * Bits 0-7 hold the vector, or the startup page number for a STARTUP IPI. */
#define APIC_ICR_VECTOR_MASK            0xFFU
#define APIC_ICR_DELIVERY_FIXED         (0U << 8)
#define APIC_ICR_DELIVERY_LOWEST        (1U << 8)
#define APIC_ICR_DELIVERY_SMI           (2U << 8)
#define APIC_ICR_DELIVERY_NMI           (4U << 8)
#define APIC_ICR_DELIVERY_INIT          (5U << 8)
#define APIC_ICR_DELIVERY_STARTUP       (6U << 8)
#define APIC_ICR_DEST_PHYSICAL          (0U << 11)
#define APIC_ICR_DEST_LOGICAL           (1U << 11)
/* Reads as 1 while a previous IPI is still being delivered. Reserved, and
 * therefore meaningless, in x2APIC mode. */
#define APIC_ICR_DELIVERY_STATUS        (1U << 12)
#define APIC_ICR_LEVEL_DEASSERT         (0U << 14)
#define APIC_ICR_LEVEL_ASSERT           (1U << 14)
#define APIC_ICR_TRIGGER_EDGE           (0U << 15)
#define APIC_ICR_TRIGGER_LEVEL          (1U << 15)
#define APIC_ICR_SHORTHAND_NONE         (0U << 18)
#define APIC_ICR_SHORTHAND_SELF         (1U << 18)
#define APIC_ICR_SHORTHAND_ALL          (2U << 18)
#define APIC_ICR_SHORTHAND_ALL_BUT_SELF (3U << 18)
/* In xAPIC the destination sits in bits 24-31 of the high half; in x2APIC the
 * high half is the full 32-bit ID with no shift. */
#define APIC_ICR_DEST_SHIFT             24

/* APIC SVR flags */
#define APIC_SVR_ENABLE      (1U << 8)
#define APIC_SVR_SPURIOUS_VECTOR  0xFF

/* APIC LVT flags */
#define APIC_LVT_MASKED               (1U << 16)
#define APIC_LVT_TIMER_PERIODIC       (1U << 17)  /* bits[18:17]=01 */
#define APIC_LVT_TIMER_TSC_DEADLINE   (2U << 17)  /* bits[18:17]=10 */

/* MSR for TSC-Deadline mode */
#define MSR_IA32_TSC_DEADLINE  0x6E0U

/* CPUID.01H:ECX bit indicating TSC-Deadline support */
#define CPUID_ECX_TSC_DEADLINE (1U << 24)

#endif /* _RODNIX_ARCH_X86_64_LAPIC_REGS_H */
