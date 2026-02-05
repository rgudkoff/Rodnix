/**
 * @file usermode.c
 * @brief Minimal ring3 entry stub (x86_64)
 */

#include "usermode.h"
#include "paging.h"
#include "pmm.h"
#include "gdt.h"
#include "types.h"
#include "../../include/common.h"
#include <stdint.h>

#define USER_CODE_VA  0x0000000000400000ULL
#define USER_STACK_VA 0x0000000000700000ULL

static const uint8_t user_stub_code[] = {
    0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00, /* mov rax, 0 */
    0xCD, 0x80,                               /* int 0x80 */
    0xEB, 0xF9                                /* jmp -7 */
};

int usermode_prepare_stub(void** entry, void** user_stack, uint64_t* rsp0_out)
{
    if (!entry || !user_stack || !rsp0_out) {
        return -1;
    }

    uint64_t code_phys = pmm_alloc_page();
    uint64_t stack_phys = pmm_alloc_page();
    if (!code_phys || !stack_phys) {
        return -1;
    }

    if (paging_map_page_4kb(USER_CODE_VA, code_phys, PTE_PRESENT | PTE_USER) != 0) {
        return -1;
    }
    if (paging_map_page_4kb(USER_STACK_VA, stack_phys, PTE_PRESENT | PTE_RW | PTE_USER) != 0) {
        return -1;
    }

    memcpy((void*)(uintptr_t)USER_CODE_VA, user_stub_code, sizeof(user_stub_code));
    memset((void*)(uintptr_t)USER_STACK_VA, 0, X86_64_PAGE_SIZE);

    *entry = (void*)(uintptr_t)USER_CODE_VA;
    *user_stack = (void*)(uintptr_t)(USER_STACK_VA + X86_64_PAGE_SIZE - 16);
    *rsp0_out = 0;
    return 0;
}

void usermode_enter(void* entry, void* user_stack, uint64_t rsp0)
{
    tss_set_rsp0(rsp0);

    uint64_t user_cs = GDT_USER_CS | 0x3;
    uint64_t user_ds = GDT_USER_DS | 0x3;

    __asm__ volatile (
        "cli\n\t"
        "movw %w0, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "pushq %0\n\t"
        "pushq %1\n\t"
        "pushq $0x202\n\t"
        "pushq %2\n\t"
        "pushq %3\n\t"
        "iretq\n\t"
        :
        : "r"(user_ds),
          "r"(user_stack),
          "r"(user_cs),
          "r"(entry)
        : "memory", "rax"
    );
}
