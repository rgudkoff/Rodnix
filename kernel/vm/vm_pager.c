#include "vm_pager.h"
#include "vm_page_ref.h"
#include "../arch/pmm.h"
#include "../arch/config.h"
#include "../../include/common.h"

uint64_t vm_pager_alloc_zero_page(void)
{
    uint64_t phys = pmm_alloc_page_in_zone(PMM_ZONE_NORMAL);
    if (!phys) {
        return 0;
    }
    void* dst = ARCH_PHYS_TO_VIRT(phys);
    memset(dst, 0, ARCH_PAGE_SIZE_4KB);
    (void)vm_page_ref_add_new(phys);
    return phys;
}
