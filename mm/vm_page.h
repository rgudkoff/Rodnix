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
 * The structure is deliberately small now and will grow. Later stages add the
 * owning object and offset (mm_redesign.md, stage 4), the queue links (stage
 * 2), the service class (stage 7), and the machine-dependent slot that a pmap
 * needs for reverse mappings (stage 8). Fields are added when something reads
 * them, not in advance.
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

typedef struct vm_page {
    /* Mappings and object references that hold this page. Atomic rather than
     * lock-protected: it is read and written on the fault path from every
     * processor, and the only ordering it needs is its own. */
    uint32_t ref_count;

    /* Nonzero means the page may never be taken back, whatever the pressure.
     * Nothing sets it yet; it is what the realtime promise will be built on. */
    uint16_t wire_count;

    uint8_t queue;   /* VM_PQ_* */
    uint8_t zone;    /* physical zone, as the allocator sees it */
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

#endif /* _RODNIX_VM_PAGE_H */
