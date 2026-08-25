/**
 * @file pmap.c
 * @brief amd64 side of the pmap contract. See mm/pmap.h.
 *
 * Everything that knows what a page table entry looks like on this machine is
 * here or below it. Nothing above the boundary composes one.
 */

#include "../../../mm/pmap.h"
#include "../../../mm/vm_map.h"      /* VM_PROT_* */
#include "paging.h"
#include "pmap_x86.h"
#include "config.h"
#include "../../fabric/spin.h"
#include "../../../lib/heap.h"
#include "../../../include/error.h"
#include "../../../include/debug.h"
#include <stddef.h>

struct pmap {
    /* Physical address of the top-level table. The one field that would have
     * been the whole type if this were a typedef of uint64_t, and the reason
     * it is not: a bare integer offers nowhere to put the lock. */
    uint64_t root;

    /* Serialises page table changes for this address space alone.
     *
     * The lock it replaces was one spinlock over every address space, so two
     * processors faulting in unrelated processes queued behind each other for
     * no reason at all. FreeBSD keeps the same lock in the same place, inside
     * struct pmap, and calls it PMAP_LOCK. */
    spinlock_t lock;

    bool is_kernel;
};

static struct pmap g_kernel_pmap;

static inline uint64_t pmap_read_cr3(void)
{
    uint64_t cr3 = 0;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void pmap_bootstrap(void)
{
    spinlock_init(&g_kernel_pmap.lock);
    g_kernel_pmap.root = pmap_read_cr3() & ~0xFFFULL;
    g_kernel_pmap.is_kernel = true;
}

pmap_t pmap_kernel(void)
{
    return &g_kernel_pmap;
}

pmap_t pmap_create(void)
{
    uint64_t root = paging_create_user_pml4();
    if (!root) {
        return NULL;
    }

    struct pmap* p = (struct pmap*)kmalloc(sizeof(struct pmap));
    if (!p) {
        paging_free_user_pml4(root);
        return NULL;
    }
    spinlock_init(&p->lock);
    p->root = root;
    p->is_kernel = false;
    return p;
}

void pmap_destroy(pmap_t pmap)
{
    if (!pmap || pmap->is_kernel) {
        return;
    }
    paging_free_user_pml4(pmap->root);
    pmap->root = 0;
    kfree(pmap);
}

/*
 * The only place in the kernel that turns intent into entry bits.
 *
 * That it is the only place is the whole of stage 3: the twelve sites that
 * used to assemble PTE_PRESENT | PTE_USER | PTE_RW themselves each had to know
 * that write is bit 1 and that no-execute is bit 63, and each was one edit
 * away from being wrong on a machine where it is neither.
 */
static uint64_t pmap_pte_bits(vm_prot_t prot, unsigned flags)
{
    uint64_t bits = PTE_PRESENT;

    if (flags & PMAP_ENTER_USER) {
        bits |= PTE_USER;
    }
    if (prot & VM_PROT_WRITE) {
        bits |= PTE_RW;
    }
    if ((prot & VM_PROT_EXEC) == 0) {
        bits |= PTE_NX;
    }
    if (flags & PMAP_ENTER_NOCACHE) {
        bits |= PTE_PCD;
    }
    return bits;
}

int pmap_enter(pmap_t pmap, vm_offset_t va, vm_paddr_t pa,
               vm_prot_t prot, unsigned flags)
{
    if (!pmap || !pmap->root) {
        return RDNX_E_INVALID;
    }

    uint64_t bits = pmap_pte_bits(prot, flags);

    spinlock_lock(&pmap->lock);
    int rc = paging_map_page_4kb_pml4(pmap->root, va, pa, bits);
    spinlock_unlock(&pmap->lock);
    return rc;
}

void pmap_remove(pmap_t pmap, vm_offset_t start, vm_offset_t end)
{
    if (!pmap || !pmap->root || end <= start) {
        return;
    }

    spinlock_lock(&pmap->lock);
    for (vm_offset_t va = start; va < end; va += PAGE_SIZE) {
        (void)paging_unmap_page_pml4(pmap->root, va);
    }
    spinlock_unlock(&pmap->lock);
}

vm_paddr_t pmap_extract(pmap_t pmap, vm_offset_t va)
{
    if (!pmap || !pmap->root) {
        return 0;
    }
    /* No lock: a translation read is a single walk, and the answer is already
     * only true until whoever asked acts on it. Taking the lock would make the
     * result no fresher and would put this on the fault path's critical
     * section for nothing. */
    return paging_get_physical_pml4(pmap->root, va) & ~(uint64_t)(PAGE_SIZE - 1);
}

void pmap_activate(pmap_t pmap)
{
    if (!pmap || !pmap->root) {
        return;
    }
    if ((pmap_read_cr3() & ~0xFFFULL) == pmap->root) {
        return;
    }
    paging_switch_pml4(pmap->root);
}

uint64_t pmap_x86_root(pmap_t pmap)
{
    return pmap ? pmap->root : 0;
}

bool pmap_is_active(pmap_t pmap)
{
    return pmap && pmap->root &&
           (pmap_read_cr3() & ~0xFFFULL) == pmap->root;
}

/* Диагностика RT-отказов: сырой срез дерева трансляции. MD-сторона,
 * потому что содержимое записей — машинная орфография. */
void pmap_debug_dump(pmap_t pmap, vm_offset_t va)
{
    if (!pmap || !pmap->root) {
        kprintf("[VMRT]   walk: no pmap\n");
        return;
    }
    uint64_t e[4];
    paging_debug_walk(pmap->root, va, e);
    kprintf("[VMRT]   walk pml4e=%llx pdpte=%llx pde=%llx pte=%llx\n",
            (unsigned long long)e[0], (unsigned long long)e[1],
            (unsigned long long)e[2], (unsigned long long)e[3]);
}
