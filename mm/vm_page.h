/**
 * @file vm_page.h
 * @brief One structure per physical page.
 *
 * Everything the kernel knows about a physical page lives here, addressed by
 * frame number. That is the arrangement both references use -- FreeBSD's
 * vm_page_array, XNU's vm_pages -- and it replaces two things we had instead:
 *
 *   - pmm_page_desc_t, an array the physical allocator kept for itself, which
 *     was skipped entirely when it did not fit in the first 16 MB, so on a
 *     machine with 4 GB there were no page descriptors at all;
 *
 *   - vm_page_ref, a side table of reference counts kept as a singly linked
 *     list with linear search and a heap allocation per referenced page --
 *     on the page fault path, and unlocked.
 *
 * The physical address is not stored. It follows from the index, because this
 * array is dense over [memory_start, memory_end). FreeBSD keeps the field
 * because its segments are sparse; ours are not, and eight bytes per page is
 * worth not spending on a value that is already known.
 *
 * The structure is deliberately small now and will grow. Stage 4 added the
 * owning object and offset below; later stages add the service class (stage
 * 7) and the machine-dependent slot that a pmap needs for reverse mappings
 * (stage 8). Fields are added when something reads them, not in advance.
 */

#ifndef _RODNIX_VM_PAGE_H
#define _RODNIX_VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Where a page sits. FREE and NONE are the only two in use today; the reclaim
 * queues arrive with the reclaim path. */
enum {
    VM_PQ_NONE = 0,   /* allocated, on no queue */
    VM_PQ_FREE,       /* in the physical allocator's free lists */
    VM_PQ_ACTIVE,
    VM_PQ_INACTIVE,
    VM_PQ_LAUNDRY,    /* dirty: needs writing before it can be reused */
};

/* Neighbour links are array indices rather than pointers: half the width, and
 * the array is the only thing they can point into anyway. */
#define VM_PAGE_NIL 0xFFFFFFFFu

/* Buddy orders. A block is 2^order pages, so this caps a block at 16 MB --
 * which is also where the low memory zone ends, and that is not a coincidence:
 * an order-12 block is 16 MB aligned, so no block can straddle the boundary
 * and coalescing never has to check. */
#define VM_NFREEORDER 13

typedef struct vm_page {
    /* Mappings and object references that hold this page. Atomic rather than
     * lock-protected: it is read and written on the fault path from every
     * processor, and the only ordering it needs is its own. */
    uint32_t ref_count;

    /* Free list neighbours. Doubly linked because coalescing removes a buddy
     * from the middle of a list, and a singly linked list would make that a
     * walk -- which would put an unbounded term back into the one path this
     * stage exists to bound. */
    uint32_t fq_next;
    uint32_t fq_prev;

    /* Nonzero means the page may never be taken back, whatever the pressure.
     * Nothing sets it yet; it is what the realtime promise will be built on. */
    uint16_t wire_count;

    uint8_t queue;   /* VM_PQ_* */
    uint8_t zone;    /* physical zone, as the allocator sees it */

    /* Size of the free block this page heads, or VM_NFREEORDER for a page that
     * heads nothing -- allocated, or in the middle of somebody else's block.
     * The two cases are told apart by searching upwards for a head that covers
     * this page, which is bounded by VM_NFREEORDER. Same encoding as FreeBSD's
     * vm_page.order, and for the same reason: marking every page of a block
     * would make freeing a large block cost its size. */
    uint8_t order;

    /* Owner and offset within it (stage 4). A page is resident in at most
     * one object; the object holds a reference for as long as the page is in
     * its index, so an indexed page cannot be freed out from under it. The
     * chain link makes the page itself the node of the object's hash bucket
     * -- the same economy as FreeBSD, where the page is the radix tree's
     * leaf: residency costs no allocation, so a fault cannot fail for want
     * of an index node. All three fields belong to the owning object's lock. */
    struct vm_object* object;
    uint32_t pindex;    /* page index within the object */
    uint32_t obj_next;  /* next page in the bucket chain, as a vm_page index */
} vm_page_t;

/*
 * Bytes of storage the array needs for a machine with this much memory. The
 * caller allocates it -- the architecture layer knows both where physical
 * memory is and how to address it, and this layer knows neither.
 */
size_t vm_page_array_bytes(uint64_t mem_start, uint64_t mem_end);

/*
 * Adopt storage for [mem_start, mem_end). Every page starts unreferenced and
 * on no queue; the allocator marks what is actually free as it builds its
 * lists.
 */
void vm_page_init(uint64_t mem_start, uint64_t mem_end, void* array);

bool vm_page_ready(void);

/* NULL for an address outside managed memory, or before vm_page_init(). Every
 * caller checks, because both cases are normal: MMIO is outside, and the early
 * boot path runs before the array exists. */
vm_page_t* vm_page_lookup(uint64_t phys);
uint64_t   vm_page_phys(const vm_page_t* m);

/* By absolute page frame number, i.e. phys >> 12. The allocator works in these
 * because buddy alignment is relative to physical zero, not to wherever
 * managed memory happens to begin. */
vm_page_t* vm_page_from_pfn(uint64_t pfn);
uint64_t   vm_page_index(const vm_page_t* m);
vm_page_t* vm_page_at_index(uint64_t index);
uint64_t   vm_page_first_pfn(void);
uint64_t   vm_page_end_pfn(void);

/*
 * Take and drop a reference. The last drop returns the page to the physical
 * allocator.
 *
 * These replace vm_page_ref_retain/release with the same meaning and none of
 * the cost: an index instead of a list walk, and no allocation, so a fault can
 * no longer fail for want of a node to describe a page that already exists.
 */
int vm_page_hold(uint64_t phys);
int vm_page_drop(uint64_t phys);
uint32_t vm_page_refs(uint64_t phys);

/*
 * Очереди подкачки (этап 7). Возврат памяти (этап 8) будет ходить по ним;
 * до него дисциплина одна: страница, установленная в отображение, стоит в
 * ACTIVE, закреплённая — ни в одной. Линки те же, что у свободных списков
 * (fq_next/fq_prev): страница либо у аллокатора, либо в очереди подкачки,
 * либо нигде — одновременно двух владельцев у линков не бывает, и каждый
 * работает под своим замком (pmm_spin / pq_spin), различая состояния по
 * полю queue.
 */

/* NONE -> ACTIVE, если страница не закреплена и ещё не в очереди. */
void vm_page_activate(uint64_t phys);

/*
 * Закрепить/открепить. Закреплённая страница покидает очереди подкачки и
 * не возвращается, что бы ни случилось с давлением, — это фундамент
 * обещания реального времени. Последнее открепление возвращает её в ACTIVE.
 */
int vm_page_wire(uint64_t phys);
int vm_page_unwire(uint64_t phys);

/* Длина очереди — для тестов и отчётов давления. */
uint32_t vm_page_queue_len(uint8_t queue);

#endif /* _RODNIX_VM_PAGE_H */
