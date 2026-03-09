#include "vm_page_ref.h"
#include "../common/heap.h"
#include "../arch/x86_64/pmm.h"
#include "../../include/common.h"
#include "../../include/error.h"

typedef struct vm_page_ref_node {
    uint64_t phys;
    uint32_t refs;
    struct vm_page_ref_node* next;
} vm_page_ref_node_t;

static vm_page_ref_node_t* g_vm_page_refs = NULL;

static vm_page_ref_node_t* vm_page_ref_find(uint64_t phys)
{
    for (vm_page_ref_node_t* it = g_vm_page_refs; it; it = it->next) {
        if (it->phys == phys) {
            return it;
        }
    }
    return NULL;
}

int vm_page_ref_add_new(uint64_t phys)
{
    if (!phys) {
        return RDNX_E_INVALID;
    }
    vm_page_ref_node_t* node = vm_page_ref_find(phys);
    if (node) {
        node->refs++;
        return RDNX_OK;
    }
    node = (vm_page_ref_node_t*)kmalloc(sizeof(vm_page_ref_node_t));
    if (!node) {
        return RDNX_E_NOMEM;
    }
    node->phys = phys;
    node->refs = 1;
    node->next = g_vm_page_refs;
    g_vm_page_refs = node;
    return RDNX_OK;
}

int vm_page_ref_retain(uint64_t phys)
{
    return vm_page_ref_add_new(phys);
}

int vm_page_ref_release(uint64_t phys)
{
    if (!phys) {
        return RDNX_E_INVALID;
    }
    vm_page_ref_node_t* prev = NULL;
    vm_page_ref_node_t* cur = g_vm_page_refs;
    while (cur) {
        if (cur->phys == phys) {
            if (cur->refs > 0) {
                cur->refs--;
            }
            if (cur->refs == 0) {
                if (prev) {
                    prev->next = cur->next;
                } else {
                    g_vm_page_refs = cur->next;
                }
                pmm_free_page(phys);
                kfree(cur);
            }
            return RDNX_OK;
        }
        prev = cur;
        cur = cur->next;
    }
    return RDNX_E_NOTFOUND;
}
