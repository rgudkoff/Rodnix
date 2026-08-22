/**
 * @file vectors.h
 * @brief Interrupt vector map, ordered by priority class.
 *
 * The local APIC delivers an interrupt only when its priority class --
 * the top nibble of the vector -- is strictly greater than the class in the
 * Task Priority Register. That is the whole mechanism behind IRQL: a level is
 * a TPR value, and raising it masks every source whose vector sits at or
 * below that class.
 *
 * Which makes the vector map a priority decision, not an allocation detail.
 * It is written down here so the two cannot drift apart:
 *
 *   0x00-0x1F  class 0-1   CPU exceptions (not delivered through the APIC)
 *   0x20-0x2F  class 2     legacy ISA IRQs, vector = 0x20 + irq
 *   0x40-0xDF  class 4-13  dynamic pool: MSI and other device interrupts
 *   0x80       class 8     syscall gate, reserved out of the pool above
 *   0xE0-0xEF  class 14    system timer
 *   0xF0-0xFE  class 15    inter-processor interrupts and the reschedule trap
 *   0xFF       class 15    spurious
 *
 * Software interrupts raised with `int n` are not subject to the TPR at all,
 * which is why the syscall gate and the reschedule trap always get through
 * regardless of level.
 */

#ifndef _RODNIX_ARCH_X86_64_VECTORS_H
#define _RODNIX_ARCH_X86_64_VECTORS_H

/* Legacy ISA range. */
#define VECTOR_ISA_BASE     0x20
#define VECTOR_ISA_LAST     0x2F

/* Dynamic device pool, minus the syscall gate carved out of it. */
#define VECTOR_DEVICE_FIRST 0x40
#define VECTOR_DEVICE_LAST  0xDF

#define VECTOR_SYSCALL      0x80

/* The PIT arrives as ISA IRQ 0 and cannot be moved; the LAPIC timer's vector
 * is ours to choose, and belongs above device interrupts. Both are ticks, so
 * both drive preemption -- see interrupt_is_tick(). */
#define VECTOR_PIT_TIMER    (VECTOR_ISA_BASE + 0)
#define VECTOR_LAPIC_TIMER  0xE0

/* Inter-processor range. */
#define VECTOR_IPI_FIRST    0xF0
#define VECTOR_IPI_LAST     0xFE
#define VECTOR_RESCHED      0xF1
#define VECTOR_SPURIOUS     0xFF

/* IRQL levels as TPR values. Each masks every vector at or below its class.
 * IRQL_HIGH has no TPR encoding: it means interrupts off outright. */
#define TPR_PASSIVE   0x00   /* nothing masked                              */
#define TPR_APC       0x10   /* nothing real yet; class 1 is unused         */
#define TPR_DISPATCH  0x20   /* legacy ISA, which includes the PIT tick     */
#define TPR_DEVICE    0xD0   /* every device interrupt, MSI included        */
#define TPR_CLOCK     0xE0   /* device interrupts and the LAPIC timer       */
#define TPR_IPI       0xF0   /* everything the APIC can deliver             */

#endif /* _RODNIX_ARCH_X86_64_VECTORS_H */
