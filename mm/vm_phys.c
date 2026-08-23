/**
 * @file vm_phys.c
 * @brief Buddy free lists over vm_page. See vm_phys.h.
 */

#include "vm_phys.h"
#include "../include/debug.h"
#include "../include/console.h"
#include <stddef.h>

#define VM_PHYS_PAGE_SHIFT 12

struct vm_freelist {
    uint32_t head;    /* array index, or VM_PAGE_NIL */
    uint64_t blocks;
};

static struct vm_freelist g_free[VM_PHYS_NZONE][VM_NFREEORDER];
static uint64_t g_free_pages[VM_PHYS_NZONE];

/*
 * Work counters, and what each one is a bound on.
 *
 * The unit that is bounded is the *block*, not the call: merging is bounded
 * because a block has one buddy per order, and splitting for the same reason.
 * A run is not bounded and was never meant to be -- it decomposes into one
 * block per 16 MB plus its two ragged ends, so handing the allocator a
 * quarter of a gigabyte at boot costs sixty-odd block operations, which is
 * proportional to the memory and entirely correct.
 *
 * Worth separating precisely because the first version of the self-test
 * conflated them, asserted a per-call bound, and failed on a bulk free at
 * boot. The assertion was wrong, not the allocator.
 */
static uint32_t g_merges;        /* in the free_block in progress */
static uint32_t g_max_merges;
static uint32_t g_max_splits;
static uint32_t g_max_run_blocks;

/*
 * The caller holds whatever lock protects physical allocation. This file takes
 * none of its own on purpose: it is one layer of one subsystem, and a lock
 * here would be a second one to order against the allocator's, for no
 * exclusion the caller does not already have.
 */

void vm_phys_init(void)
{
    for (int z = 0; z < VM_PHYS_NZONE; z++) {
        g_free_pages[z] = 0;
        for (int o = 0; o < VM_NFREEORDER; o++) {
            g_free[z][o].head = VM_PAGE_NIL;
            g_free[z][o].blocks = 0;
        }
    }
}

static inline uint64_t vm_phys_pfn(const vm_page_t* m)
{
    return vm_page_phys(m) >> VM_PHYS_PAGE_SHIFT;
}

static void vm_freelist_add(vm_page_t* m, uint8_t zone, uint32_t order)
{
    struct vm_freelist* fl = &g_free[zone][order];
    uint32_t idx = (uint32_t)vm_page_index(m);

    m->fq_prev = VM_PAGE_NIL;
    m->fq_next = fl->head;
    if (fl->head != VM_PAGE_NIL) {
        vm_page_at_index(fl->head)->fq_prev = idx;
    }
    fl->head = idx;
    fl->blocks++;

    m->order = (uint8_t)order;
    m->queue = VM_PQ_FREE;
    g_free_pages[zone] += (1ULL << order);
}

static void vm_freelist_rem(vm_page_t* m, uint8_t zone, uint32_t order)
{
    struct vm_freelist* fl = &g_free[zone][order];

    if (m->fq_prev != VM_PAGE_NIL) {
        vm_page_at_index(m->fq_prev)->fq_next = m->fq_next;
    } else {
        fl->head = m->fq_next;
    }
    if (m->fq_next != VM_PAGE_NIL) {
        vm_page_at_index(m->fq_next)->fq_prev = m->fq_prev;
    }
    fl->blocks--;

    m->fq_next = VM_PAGE_NIL;
    m->fq_prev = VM_PAGE_NIL;
    m->order = VM_NFREEORDER;
    m->queue = VM_PQ_NONE;
    g_free_pages[zone] -= (1ULL << order);
}

/*
 * Give a block back, merging with its buddy for as long as the buddy is free
 * and the same size.
 *
 * The buddy of a block is its address with one bit flipped, which is the whole
 * reason this is bounded: there is exactly one candidate per order and at most
 * VM_NFREEORDER orders.
 */
static void vm_phys_free_block(uint64_t pfn, uint32_t order)
{
    vm_page_t* m = vm_page_from_pfn(pfn);
    if (!m) {
        return;
    }
    uint8_t zone = m->zone;
    if (zone >= VM_PHYS_NZONE) {
        return;
    }

    g_merges = 0;
    while (order + 1 < VM_NFREEORDER) {
        uint64_t buddy_pfn = pfn ^ (1ULL << order);
        vm_page_t* buddy = vm_page_from_pfn(buddy_pfn);

        /* Outside managed memory, in another zone, in use, or a different
         * size -- any of those and there is nothing to merge with. The zone
         * test is belt and braces: a block is naturally aligned and no zone
         * boundary falls inside one, so this cannot differ. */
        if (!buddy || buddy->zone != zone ||
            buddy->queue != VM_PQ_FREE || buddy->order != order) {
            break;
        }

        vm_freelist_rem(buddy, zone, order);
        if (buddy_pfn < pfn) {
            pfn = buddy_pfn;
            m = buddy;
        }
        order++;
        g_merges++;
    }

    if (g_merges > g_max_merges) {
        g_max_merges = g_merges;
    }
    vm_freelist_add(m, zone, order);
}

void vm_phys_free_run(uint64_t phys, uint64_t npages)
{
    uint64_t pfn = phys >> VM_PHYS_PAGE_SHIFT;
    uint32_t blocks = 0;

    while (npages > 0) {
        /* The largest block that both starts here and fits. Alignment first,
         * because a block that is not naturally aligned has no buddy. */
        uint32_t order = 0;
        while (order + 1 < VM_NFREEORDER &&
               (pfn & ((1ULL << (order + 1)) - 1)) == 0 &&
               (1ULL << (order + 1)) <= npages) {
            order++;
        }
        vm_phys_free_block(pfn, order);
        pfn += (1ULL << order);
        npages -= (1ULL << order);
        blocks++;
    }

    if (blocks > g_max_run_blocks) {
        g_max_run_blocks = blocks;
    }
}

uint64_t vm_phys_alloc(uint8_t zone, uint32_t npages)
{
    if (zone >= VM_PHYS_NZONE || npages == 0) {
        return 0;
    }

    uint32_t want = 0;
    while ((1ULL << want) < (uint64_t)npages) {
        want++;
        if (want >= VM_NFREEORDER) {
            return 0;   /* larger than any block this allocator forms */
        }
    }

    uint32_t order = want;
    while (order < VM_NFREEORDER && g_free[zone][order].head == VM_PAGE_NIL) {
        order++;
    }

    if (order >= VM_NFREEORDER) {
        return 0;
    }

    vm_page_t* m = vm_page_at_index(g_free[zone][order].head);
    vm_freelist_rem(m, zone, order);
    uint64_t pfn = vm_phys_pfn(m);

    /* Split down, returning the half we do not need at each step. */
    uint32_t splits = order - want;
    if (splits > g_max_splits) {
        g_max_splits = splits;
    }
    while (order > want) {
        order--;
        vm_phys_free_block(pfn + (1ULL << order), order);
    }

    /* And hand back the rounding, so asking for three pages costs three and
     * not four. */
    uint64_t got = 1ULL << want;
    if (got > npages) {
        vm_phys_free_run((pfn + npages) << VM_PHYS_PAGE_SHIFT, got - npages);
    }

    return pfn << VM_PHYS_PAGE_SHIFT;
}

/*
 * Find the free block containing this page, if any.
 *
 * Only the head of a block carries its order; every other page carries
 * VM_NFREEORDER. So the search walks upwards through the aligned bases: the
 * page is free exactly when one of them is a head whose block reaches far
 * enough to cover it. Bounded by VM_NFREEORDER, and it is what makes marking
 * every page of a block unnecessary -- freeing a 16 MB block would otherwise
 * cost four thousand writes.
 *
 * This is FreeBSD's vm_phys_unfree_page() search, and the encoding it implies.
 */
static vm_page_t* vm_phys_find_block(uint64_t pfn, uint32_t* out_order)
{
    for (uint32_t order = 0; order < VM_NFREEORDER; order++) {
        uint64_t base = pfn & ~((1ULL << order) - 1);
        vm_page_t* head = vm_page_from_pfn(base);
        if (!head) {
            return NULL;
        }
        if (head->queue == VM_PQ_FREE && head->order != VM_NFREEORDER) {
            if (head->order < order) {
                return NULL;   /* a block, but it stops short of this page */
            }
            *out_order = head->order;
            return head;
        }
    }
    return NULL;
}

bool vm_phys_is_free(uint64_t phys)
{
    uint32_t order;
    return vm_phys_find_block(phys >> VM_PHYS_PAGE_SHIFT, &order) != NULL;
}

bool vm_phys_unfree(uint64_t phys)
{
    uint64_t pfn = phys >> VM_PHYS_PAGE_SHIFT;
    uint32_t order = 0;
    vm_page_t* head = vm_phys_find_block(pfn, &order);
    if (!head) {
        return false;
    }

    uint8_t zone = head->zone;
    uint64_t base = vm_phys_pfn(head);
    vm_freelist_rem(head, zone, order);

    /* Shrink towards the wanted page, giving back the half that misses it. */
    while (order > 0) {
        order--;
        uint64_t half = base + (1ULL << order);
        if (pfn < half) {
            vm_phys_free_block(half, order);
        } else {
            vm_phys_free_block(base, order);
            base = half;
        }
    }

    return true;
}

uint32_t vm_phys_block_first(uint8_t zone, uint32_t order)
{
    if (zone >= VM_PHYS_NZONE || order >= VM_NFREEORDER) {
        return VM_PAGE_NIL;
    }
    return g_free[zone][order].head;
}

uint32_t vm_phys_block_next(uint32_t index)
{
    vm_page_t* m = vm_page_at_index(index);
    return m ? m->fq_next : VM_PAGE_NIL;
}

uint64_t vm_phys_unfree_range(uint64_t phys, uint64_t npages)
{
    uint64_t pfn = phys >> VM_PHYS_PAGE_SHIFT;
    uint64_t end = pfn + npages;
    uint64_t taken = 0;

    while (pfn < end) {
        uint32_t order = 0;
        vm_page_t* head = vm_phys_find_block(pfn, &order);
        if (!head) {
            pfn++;          /* already in use */
            continue;
        }

        uint8_t zone = head->zone;
        uint64_t base = vm_phys_pfn(head);
        uint64_t limit = base + (1ULL << order);
        vm_freelist_rem(head, zone, order);

        /* Whatever of this block lies outside the range goes straight back;
         * the overlap is what we came for. */
        if (base < pfn) {
            vm_phys_free_run(base << VM_PHYS_PAGE_SHIFT, pfn - base);
        }
        uint64_t stop = (limit < end) ? limit : end;
        if (limit > stop) {
            vm_phys_free_run(stop << VM_PHYS_PAGE_SHIFT, limit - stop);
        }

        taken += stop - pfn;
        pfn = stop;
    }

    return taken;
}

void vm_phys_reset_stats(void)
{
    g_max_splits = 0;
    g_max_merges = 0;
    g_max_run_blocks = 0;
}

uint32_t vm_phys_max_splits(void)
{
    return g_max_splits;
}

uint32_t vm_phys_max_merges(void)
{
    return g_max_merges;
}

uint32_t vm_phys_max_run_blocks(void)
{
    return g_max_run_blocks;
}

/*
 * Fragment memory as hard as a single-page allocation pattern can, then check
 * that allocation still costs no more than it did when memory was whole.
 *
 * Fragmenting by freeing alternate pages is the worst case on purpose: every
 * free block is order zero, coalescing never fires, and the free lists are as
 * long as they can be. The old allocator's answer to that state was to scan
 * the whole page bitmap; the claim here is that the answer is unchanged.
 */
/*
 * One round of the work being compared: single pages, and a block big enough
 * that the allocator has to split something to produce it. Both, because a
 * single page usually finds an order-zero block waiting and then the splitting
 * path is never entered -- and an untested bound is not a bound.
 */
static void vm_phys_cycle(uint32_t rounds)
{
    for (uint32_t i = 0; i < rounds; i++) {
        uint64_t p = vm_phys_alloc(1, 1);
        if (p) {
            vm_phys_free_run(p, 1);
        }
        uint64_t big = vm_phys_alloc(1, 256);
        if (big) {
            vm_phys_free_run(big, 256);
        }
    }
}

void vm_phys_selftest(void)
{
    enum { N = 512, CYCLES = 64 };
    static uint64_t got[N];

    uint64_t before = vm_phys_free_count(1);

    /* Phase one: memory as the allocator found it. */
    vm_phys_reset_stats();
    vm_phys_cycle(CYCLES);
    uint32_t splits_whole = g_max_splits;
    uint32_t merges_whole = g_max_merges;

    /* Phase two: fragment it as hard as single pages allow. Freeing alternate
     * pages leaves every free block at order zero, so nothing can coalesce and
     * the lists are as long as they get. */
    uint32_t n = 0;
    for (uint32_t i = 0; i < N; i++) {
        got[i] = vm_phys_alloc(1, 1);
        if (!got[i]) {
            break;
        }
        n++;
    }
    for (uint32_t i = 0; i < n; i += 2) {
        vm_phys_free_run(got[i], 1);
        got[i] = 0;
    }

    /* Phase three: the same work, with those blocks in the way. This is the
     * comparison the stage exists to make -- the old allocator answered this
     * state by scanning every page of memory. */
    vm_phys_reset_stats();
    vm_phys_cycle(CYCLES);
    uint32_t splits_frag = g_max_splits;
    uint32_t merges_frag = g_max_merges;

    /* Phase four: give the rest back. Every page now has a free buddy, so
     * this is where merging actually runs and where its bound is tested. */
    vm_phys_reset_stats();
    for (uint32_t i = 0; i < n; i++) {
        if (got[i]) {
            vm_phys_free_run(got[i], 1);
            got[i] = 0;
        }
    }
    uint32_t merges_deep = g_max_merges;

    uint64_t after = vm_phys_free_count(1);

    kprintf("[vm_phys] selftest: %u pages, %u free blocks in the way; "
            "splits %u whole -> %u fragmented; merges %u / %u / %u deep; "
            "bound %u; free %llu -> %llu\n",
            (unsigned)n, (unsigned)(n / 2u),
            (unsigned)splits_whole, (unsigned)splits_frag,
            (unsigned)merges_whole, (unsigned)merges_frag, (unsigned)merges_deep,
            (unsigned)(VM_NFREEORDER - 1),
            (unsigned long long)before, (unsigned long long)after);

    if (after != before) {
        panicf("vm_phys selftest: %llu pages lost or gained over a full cycle",
               (unsigned long long)(before > after ? before - after
                                                   : after - before));
    }
    if (splits_frag > VM_NFREEORDER - 1 || merges_frag > VM_NFREEORDER - 1 ||
        merges_deep > VM_NFREEORDER - 1) {
        panicf("vm_phys selftest: %u splits, %u/%u merges, past the bound of %u",
               (unsigned)splits_frag, (unsigned)merges_frag,
               (unsigned)merges_deep, (unsigned)(VM_NFREEORDER - 1));
    }
    if (merges_deep == 0) {
        /* Nothing coalesced, so the bound on merging was never tested and the
         * line above proves nothing about it. */
        panicf("vm_phys selftest: no merging happened, the test did not test it");
    }
}

uint64_t vm_phys_free_count(uint8_t zone)
{
    return (zone < VM_PHYS_NZONE) ? g_free_pages[zone] : 0;
}

uint64_t vm_phys_free_blocks(uint8_t zone, uint32_t order)
{
    if (zone >= VM_PHYS_NZONE || order >= VM_NFREEORDER) {
        return 0;
    }
    return g_free[zone][order].blocks;
}
