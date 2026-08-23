/**
 * @file pmap.h
 * @brief The boundary between what memory means and how a machine spells it.
 *
 * Above this line: regions, objects, mapped files, copy-on-write, protection.
 * Below it: page table entries, TLBs, and the bit that means "no execute".
 * The line is the same one FreeBSD draws in sys/vm/pmap.h and XNU in
 * osfmk/vm/pmap.h, and both inherited it from Mach, because the split has
 * turned out to be the right one three times.
 *
 * What makes it a boundary rather than a convention is the shape of the types.
 * An address space arrives as an opaque pmap_t, never as the physical address
 * of a page table root. Protection arrives as vm_prot_t, never as assembled
 * entry bits. The upper layer physically cannot compose an x86 page table
 * entry, which is the point -- until now it did, in thirty-seven places, and
 * the arm64 port in arm64_bootstrap.md was therefore not "write a second pmap"
 * but "rewrite mm/".
 *
 * The lock lives here too. FreeBSD keeps PMAP_LOCK inside struct pmap, so page
 * tables are serialised per address space rather than by one lock over the
 * whole machine, which is what we had.
 */

#ifndef _RODNIX_VM_PMAP_H
#define _RODNIX_VM_PMAP_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t vm_paddr_t;
typedef uint64_t vm_offset_t;
typedef uint32_t vm_prot_t;   /* VM_PROT_* from vm_map.h */

/* Opaque by design: the definition is per-architecture and this layer has no
 * business seeing it. */
struct pmap;
typedef struct pmap* pmap_t;

/* Options to pmap_enter(). Expressed as intent, not as hardware bits: what a
 * given machine does about "do not cache this" is its own business. */
#define PMAP_ENTER_USER     (1u << 0)   /* reachable from ring 3 */
#define PMAP_ENTER_NOCACHE  (1u << 1)   /* device memory; no caching */

void   pmap_bootstrap(void);

/* A fresh address space, or NULL. Carries the kernel's own mappings, since
 * every address space must. */
pmap_t pmap_create(void);

/* Release it and everything private to it. The pages it mapped are the
 * caller's to account for; this frees the tables, not the memory they
 * described. */
void   pmap_destroy(pmap_t pmap);

/* The kernel's address space, and the one this processor is running on. */
pmap_t pmap_kernel(void);

/* Establish, remove and interrogate one translation. */
int        pmap_enter(pmap_t pmap, vm_offset_t va, vm_paddr_t pa,
                      vm_prot_t prot, unsigned flags);
void       pmap_remove(pmap_t pmap, vm_offset_t start, vm_offset_t end);
vm_paddr_t pmap_extract(pmap_t pmap, vm_offset_t va);

/* Run on it. */
void   pmap_activate(pmap_t pmap);
bool   pmap_is_active(pmap_t pmap);

/*
 * pmap_enter() takes a physical address rather than the vm_page_t that
 * FreeBSD's takes, and the reason is staging rather than preference: objects
 * still hold physical addresses (mm_redesign.md, stage 4), and some mappings
 * -- a framebuffer -- describe memory that has no vm_page at all and never
 * will. It becomes vm_page_t when the object layer does.
 */

#endif /* _RODNIX_VM_PMAP_H */
