/**
 * @file vm_page.c
 * @brief One structure per physical page. See vm_page.h.
 */

#include "vm_page.h"
#include "../include/console.h"
#include "../kernel/arch/pmm.h"
#include "../include/error.h"
#include "../include/debug.h"

#define VM_PAGE_SHIFT 12
#define VM_PAGE_BYTES (1ULL << VM_PAGE_SHIFT)

static vm_page_t* g_pages;
static uint64_t g_mem_start;
static uint64_t g_mem_end;
static uint64_t g_count;

size_t vm_page_array_bytes(uint64_t mem_start, uint64_t mem_end)
{
    if (mem_end <= mem_start) {
        return 0;
    }
    uint64_t pages = (mem_end - mem_start) >> VM_PAGE_SHIFT;
    return (size_t)(pages * sizeof(vm_page_t));
}

void vm_page_init(uint64_t mem_start, uint64_t mem_end, void* array)
{
    if (!array || mem_end <= mem_start) {
        return;
    }

    g_mem_start = mem_start;
    g_mem_end = mem_end;
    g_count = (mem_end - mem_start) >> VM_PAGE_SHIFT;
    g_pages = (vm_page_t*)array;

    for (uint64_t i = 0; i < g_count; i++) {
        g_pages[i].ref_count = 0;
        g_pages[i].fq_next = VM_PAGE_NIL;
        g_pages[i].fq_prev = VM_PAGE_NIL;
        g_pages[i].wire_count = 0;
        g_pages[i].queue = VM_PQ_NONE;
        g_pages[i].zone = 0;
        g_pages[i].order = VM_NFREEORDER;
    }
}

bool vm_page_ready(void)
{
    return g_pages != NULL;
}

vm_page_t* vm_page_lookup(uint64_t phys)
{
    if (!g_pages || phys < g_mem_start || phys >= g_mem_end) {
        return NULL;
    }
    return &g_pages[(phys - g_mem_start) >> VM_PAGE_SHIFT];
}

uint64_t vm_page_phys(const vm_page_t* m)
{
    if (!g_pages || !m) {
        return 0;
    }
    return g_mem_start + ((uint64_t)(m - g_pages) << VM_PAGE_SHIFT);
}

vm_page_t* vm_page_from_pfn(uint64_t pfn)
{
    return vm_page_lookup(pfn << VM_PAGE_SHIFT);
}

uint64_t vm_page_index(const vm_page_t* m)
{
    return (!g_pages || !m) ? 0 : (uint64_t)(m - g_pages);
}

vm_page_t* vm_page_at_index(uint64_t index)
{
    return (g_pages && index < g_count) ? &g_pages[index] : NULL;
}

uint64_t vm_page_first_pfn(void)
{
    return g_mem_start >> VM_PAGE_SHIFT;
}

uint64_t vm_page_end_pfn(void)
{
    return g_mem_end >> VM_PAGE_SHIFT;
}

int vm_page_hold(uint64_t phys)
{
    if (!phys) {
        return RDNX_E_INVALID;
    }
    vm_page_t* m = vm_page_lookup(phys);
    if (!m) {
        /*
         * Outside managed memory -- a framebuffer or another device mapping.
         * Those are not allocated from the physical allocator and are never
         * returned to it, so there is nothing to count. Not an error: the
         * mapping paths hold and drop every page they touch without knowing
         * which kind it is, and that is the point of them not knowing.
         */
        return RDNX_OK;
    }
    __atomic_add_fetch(&m->ref_count, 1u, __ATOMIC_ACQ_REL);
    return RDNX_OK;
}

int vm_page_drop(uint64_t phys)
{
    if (!phys) {
        return RDNX_E_INVALID;
    }
    vm_page_t* m = vm_page_lookup(phys);
    if (!m) {
        return RDNX_OK;   /* see vm_page_hold() */
    }

    uint32_t old = __atomic_load_n(&m->ref_count, __ATOMIC_ACQUIRE);
    for (;;) {
        if (old == 0) {
            /* Dropping a reference nobody took. Worth a word rather than a
             * silent wrap to four billion. */
            DEBUG_WARN("vm_page: drop of unreferenced page %llx",
                       (unsigned long long)phys);
            return RDNX_E_NOTFOUND;
        }
        if (__atomic_compare_exchange_n(&m->ref_count, &old, old - 1u, true,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            break;
        }
    }

    if (old == 1u) {
        /* This processor took the count to zero, so it owns the free. The
         * compare-exchange is what makes that unambiguous with several
         * processors dropping at once. */
        if (m->object != NULL) {
            /* An indexed page reached zero references, which means somebody
             * dropped a reference they did not own: the owning object holds
             * one for as long as the page is in its index. Freeing it now
             * hands the allocator a page that a hash chain still points at,
             * and the next allocation corrupts that chain. Loud, always. */
            kprintf("[VMPAGE] freeing page %llx still indexed (pindex=%u)\n",
                    (unsigned long long)phys, m->pindex);
        }
        pmm_free_page(phys);
    }
    return RDNX_OK;
}

uint32_t vm_page_refs(uint64_t phys)
{
    vm_page_t* m = vm_page_lookup(phys);
    if (!m) {
        return 0;
    }
    return __atomic_load_n(&m->ref_count, __ATOMIC_ACQUIRE);
}
