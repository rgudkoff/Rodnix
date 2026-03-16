#ifndef _RODNIX_VM_PAGE_REF_H
#define _RODNIX_VM_PAGE_REF_H

#include <stdint.h>

int vm_page_ref_add_new(uint64_t phys);
int vm_page_ref_retain(uint64_t phys);
int vm_page_ref_release(uint64_t phys);

#endif /* _RODNIX_VM_PAGE_REF_H */
