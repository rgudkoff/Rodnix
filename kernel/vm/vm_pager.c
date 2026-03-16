#include "vm_pager.h"
#include "../arch/x86_64/pmm.h"
#include "../arch/x86_64/config.h"
#include "../../include/common.h"

uint64_t vm_pager_alloc_zero_page(void)
{
    uint64_t phys = pmm_alloc_page_in_zone(PMM_ZONE_NORMAL);
    if (!phys) {
        return 0;
    }
    void* dst = X86_64_PHYS_TO_VIRT(phys);
    memset(dst, 0, X86_64_PAGE_SIZE_4KB);
    return phys;
}

