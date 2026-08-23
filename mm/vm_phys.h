/**
 * @file vm_phys.h
 * @brief The physical page allocator: buddy free lists over vm_page.
 *
 * Machine-independent, and that is the point of where it lives. Deciding which
 * physical pages are free is about frames and nothing else; what a page table
 * entry looks like belongs on the other side of the pmap boundary. FreeBSD
 * makes the same split -- this is sys/vm/vm_phys.c, not sys/amd64/amd64.
 *
 * Why buddy rather than what it replaces. The old allocator kept a fixed array
 * of 128 free ranges per zone and silently dropped anything past that; when
 * they no longer described enough memory it fell through to scanning the whole
 * page bitmap, and then rebuilt every list from it -- twice O(all memory), for
 * one allocation, with interrupts masked.
 *
 * That is not merely slow. This system's whole argument is a bound on
 * worst-case latency, and an allocator whose cost depends on how fragmented
 * memory happens to be has no worst case worth stating. Here, allocation and
 * free are both bounded by VM_NFREEORDER -- thirteen splits or thirteen
 * merges, whatever the state of memory.
 */

#ifndef _RODNIX_VM_PHYS_H
#define _RODNIX_VM_PHYS_H

#include "vm_page.h"
#include <stdbool.h>
#include <stdint.h>

/* Matches the zone count the architecture layer uses. Zones exist because some
 * memory is special to the hardware, which is an architecture question; the
 * allocator only keeps them apart. */
#define VM_PHYS_NZONE 3

void vm_phys_init(void);

/* Hand a run of pages to the allocator. Split into naturally aligned blocks,
 * which is why an arbitrary run costs no more than the blocks it decomposes
 * into. Used both at boot and to free an allocation. */
void vm_phys_free_run(uint64_t phys, uint64_t npages);

/* Contiguous allocation, zero on failure. `npages` need not be a power of two:
 * the smallest block that fits is taken and the excess handed straight back,
 * so the caller is not charged for the rounding. */
uint64_t vm_phys_alloc(uint8_t zone, uint32_t npages);

/* Take one specific page out of the free lists, splitting whatever block
 * contains it. False if it was not free. This is what lets a range be reserved
 * after the lists are built, which a buddy does not otherwise offer. */
bool vm_phys_unfree(uint64_t phys);

/*
 * Take a whole range out, and do it in blocks rather than a page at a time.
 *
 * Worth its own function because the page-at-a-time version was measurably
 * wrong: reserving the kernel image took 1179 us with interrupts masked,
 * against 310 us for the list-based allocator it replaced. A buddy splits a
 * 16 MB block into twelve to isolate one page, and doing that five thousand
 * times to reserve five thousand contiguous pages is work proportional to the
 * range times the order count.
 *
 * Here each free block that overlaps the range is removed once, and the parts
 * of it outside the range are handed straight back. The cost is proportional
 * to the number of blocks touched, not to the number of pages.
 *
 * Returns how many pages it actually took, so the caller can account for them.
 */
uint64_t vm_phys_unfree_range(uint64_t phys, uint64_t npages);

bool vm_phys_is_free(uint64_t phys);

/*
 * Walk the free blocks of one zone and order. Returns array indices, or
 * VM_PAGE_NIL when there are no more.
 *
 * Offered because the alternative is what the first version of the caller did:
 * step over every frame in memory looking for block heads. That is O(memory)
 * to answer a question the lists already hold the answer to, and putting an
 * O(memory) walk back into this file would undo the point of it.
 */
uint32_t vm_phys_block_first(uint8_t zone, uint32_t order);
uint32_t vm_phys_block_next(uint32_t index);

/*
 * The most splitting one allocation, and the most merging one block free, has
 * cost since boot -- plus how many blocks the largest run decomposed into.
 *
 * Counted rather than timed. The claim this allocator exists to make is a
 * bound on work, and a bound on work is exactly what a clock cannot show --
 * ours has already been caught reporting twelve seconds for a window in which
 * the timer tick did not advance at all. Splits and merges are countable,
 * deterministic, and immune to the emulator.
 *
 * Splits and merges are bounded by the order count however fragmented memory
 * is. Run blocks are not bounded and are not meant to be: a run decomposes
 * into one block per 16 MB, so handing over a quarter of a gigabyte at boot is
 * sixty-odd blocks, proportional to the memory and correct.
 */
void vm_phys_reset_stats(void);
uint32_t vm_phys_max_splits(void);
uint32_t vm_phys_max_merges(void);
uint32_t vm_phys_max_run_blocks(void);

/* Prove the bound holds under fragmentation. Runs on rdnx.vmphys=selftest,
 * panics if it does not. */
void vm_phys_selftest(void);

uint64_t vm_phys_free_count(uint8_t zone);
uint64_t vm_phys_free_blocks(uint8_t zone, uint32_t order);

#endif /* _RODNIX_VM_PHYS_H */
