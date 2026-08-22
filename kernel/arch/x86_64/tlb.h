/**
 * @file tlb.h
 * @brief Cross-processor TLB invalidation.
 *
 * A processor that changes a page table entry flushes only its own TLB. Every
 * other processor that has ever run that address space may still hold the old
 * translation, and nothing tells it otherwise.
 *
 * That is not a performance detail. Once the physical page behind a removed
 * mapping is freed and handed to something else -- a page table, say -- a
 * processor still translating through the stale entry writes data into it.
 * The next walk of that table then finds reserved bits set, which is how this
 * surfaced: a user page fault whose error code said "reserved bit violation".
 *
 * Both references treat this as a first-class mechanism with acknowledgement.
 * FreeBSD publishes the operation, sends IPI_INVLOP, and spins until every
 * target has written the current generation into its scoreboard slot before
 * considering the change complete (sys/amd64/amd64/mp_machdep.c). XNU signals
 * the same way through cpu_signal_handler. Waiting for the acknowledgement is
 * the point: without it, "the mapping is gone" is only true locally, and
 * freeing the page afterwards is unsafe.
 */

#ifndef _RODNIX_ARCH_X86_64_TLB_H
#define _RODNIX_ARCH_X86_64_TLB_H

#include <stdint.h>

/* Invalidate one page everywhere, and wait until every processor confirms.
 * `va` of zero means a full flush. Returns once the mapping is provably gone
 * from every TLB, which is what makes it safe to release the page.
 *
 * Must not be called while holding a lock that another processor might be
 * spinning on with interrupts masked: that processor could not take the IPI,
 * and the wait would never end. */
void tlb_shootdown(uint64_t va);

/* Install the IPI handler. Called once, after interrupt vectors exist. */
void tlb_shootdown_init(void);

#endif /* _RODNIX_ARCH_X86_64_TLB_H */
