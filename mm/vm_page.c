/**
 * @file vm_page.c
 * @brief One structure per physical page. See vm_page.h.
 */

#include "vm_page.h"
#include "../include/console.h"
#include "../kernel/fabric/spin.h"
#include "../kernel/arch/pmm.h"
#include "../include/error.h"
#include "../include/debug.h"
#include "../trace/bootlog.h"

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

static void vm_page_queue_leave(vm_page_t* m);

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
        vm_page_queue_leave(m);
        if (m->object != NULL) {
            /* An indexed page reached zero references, which means somebody
             * dropped a reference they did not own: the owning object holds
             * one for as long as the page is in its index. Freeing it now
             * hands the allocator a page that a hash chain still points at,
             * and the next allocation corrupts that chain. Loud, always. */
            klog_err("vm", "freeing page %llx still indexed (pindex=%u)\n",
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

/* ============================================================================
 * Очереди подкачки.
 *
 * Один замок на все три: операции — пара сшивок указателей, а ходить по
 * очередям начнёт только возврат памяти (этап 8). Разделение — когда
 * будет измерена конкуренция, не раньше.
 * ============================================================================ */

static spinlock_t pq_spin;

typedef struct vm_pagequeue {
    uint32_t head;   /* индекс vm_page или VM_PAGE_NIL */
    uint32_t tail;
    uint32_t count;
} vm_pagequeue_t;

/* Нулевая инициализация здесь была бы ошибкой: 0 — законный индекс
 * страницы. Пустота — VM_PAGE_NIL, выставляется при первом обращении. */
static vm_pagequeue_t g_pq[3] = {
    { VM_PAGE_NIL, VM_PAGE_NIL, 0 },
    { VM_PAGE_NIL, VM_PAGE_NIL, 0 },
    { VM_PAGE_NIL, VM_PAGE_NIL, 0 },
}; /* [0]=ACTIVE [1]=INACTIVE [2]=LAUNDRY */

static vm_pagequeue_t* pq_of(uint8_t queue)
{
    switch (queue) {
    case VM_PQ_ACTIVE:   return &g_pq[0];
    case VM_PQ_INACTIVE: return &g_pq[1];
    case VM_PQ_LAUNDRY:  return &g_pq[2];
    default:             return NULL;
    }
}

/* Вставка в хвост. Вызывающий держит pq_spin; страница ни в одной очереди. */
static void pq_insert_locked(vm_pagequeue_t* q, vm_page_t* m, uint8_t queue)
{
    uint32_t idx = (uint32_t)vm_page_index(m);
    m->fq_next = VM_PAGE_NIL;
    m->fq_prev = q->tail;
    if (q->tail != VM_PAGE_NIL) {
        vm_page_at_index(q->tail)->fq_next = idx;
    } else {
        q->head = idx;
    }
    q->tail = idx;
    q->count++;
    m->queue = queue;
}

/* Извлечение. Вызывающий держит pq_spin; страница в очереди q. */
static void pq_remove_locked(vm_pagequeue_t* q, vm_page_t* m)
{
    if (m->fq_prev != VM_PAGE_NIL) {
        vm_page_at_index(m->fq_prev)->fq_next = m->fq_next;
    } else {
        q->head = m->fq_next;
    }
    if (m->fq_next != VM_PAGE_NIL) {
        vm_page_at_index(m->fq_next)->fq_prev = m->fq_prev;
    } else {
        q->tail = (m->fq_prev == VM_PAGE_NIL && q->head == VM_PAGE_NIL)
                      ? VM_PAGE_NIL : m->fq_prev;
    }
    if (q->head == VM_PAGE_NIL) {
        q->tail = VM_PAGE_NIL;
    }
    m->fq_next = VM_PAGE_NIL;
    m->fq_prev = VM_PAGE_NIL;
    m->queue = VM_PQ_NONE;
    if (q->count > 0) {
        q->count--;
    }
}

/* Покинуть очередь подкачки, в какой бы страница ни стояла. Зовётся перед
 * возвратом аллокатору: свободный список сейчас переиспользует линки. */
static void vm_page_queue_leave(vm_page_t* m)
{
    vm_pagequeue_t* q = pq_of(m->queue);
    if (!q) {
        return;
    }
    uint64_t f = spinlock_lock_irqsave(&pq_spin);
    q = pq_of(m->queue);   /* перечитать под замком */
    if (q) {
        pq_remove_locked(q, m);
    }
    spinlock_unlock_irqrestore(&pq_spin, f);
}

void vm_page_activate(uint64_t phys)
{
    vm_page_t* m = vm_page_lookup(phys);
    if (!m) {
        return;
    }
    uint64_t f = spinlock_lock_irqsave(&pq_spin);
    if (m->queue == VM_PQ_NONE && m->wire_count == 0) {
        pq_insert_locked(pq_of(VM_PQ_ACTIVE), m, VM_PQ_ACTIVE);
    }
    spinlock_unlock_irqrestore(&pq_spin, f);
}

int vm_page_wire(uint64_t phys)
{
    vm_page_t* m = vm_page_lookup(phys);
    if (!m) {
        return RDNX_E_INVALID;
    }
    uint64_t f = spinlock_lock_irqsave(&pq_spin);
    m->wire_count++;
    if (m->wire_count == 1) {
        vm_pagequeue_t* q = pq_of(m->queue);
        if (q) {
            pq_remove_locked(q, m);
        }
    }
    spinlock_unlock_irqrestore(&pq_spin, f);
    return RDNX_OK;
}

int vm_page_unwire(uint64_t phys)
{
    vm_page_t* m = vm_page_lookup(phys);
    if (!m) {
        return RDNX_E_INVALID;
    }
    uint64_t f = spinlock_lock_irqsave(&pq_spin);
    if (m->wire_count == 0) {
        spinlock_unlock_irqrestore(&pq_spin, f);
        DEBUG_WARN("vm_page: unwire of unwired page %llx",
                   (unsigned long long)phys);
        return RDNX_E_NOTFOUND;
    }
    m->wire_count--;
    if (m->wire_count == 0 && m->queue == VM_PQ_NONE) {
        pq_insert_locked(pq_of(VM_PQ_ACTIVE), m, VM_PQ_ACTIVE);
    }
    spinlock_unlock_irqrestore(&pq_spin, f);
    return RDNX_OK;
}

uint32_t vm_page_queue_len(uint8_t queue)
{
    vm_pagequeue_t* q = pq_of(queue);
    if (!q) {
        return 0;
    }
    uint64_t f = spinlock_lock_irqsave(&pq_spin);
    uint32_t n = q->count;
    spinlock_unlock_irqrestore(&pq_spin, f);
    return n;
}
