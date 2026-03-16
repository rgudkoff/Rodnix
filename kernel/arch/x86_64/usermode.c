/**
 * @file usermode.c
 * @brief Minimal ring3 entry stub (x86_64)
 */

#include "usermode.h"
#include "paging.h"
#include "pmm.h"
#include "gdt.h"
#include "types.h"
#include "config.h"
#include "../../common/bootlog.h"
#include "../../include/common.h"
#include "../../include/error.h"
#include <stdint.h>

#define USER_CODE_VA  0x0000000040000000ULL
#define USER_STACK_VA 0x0000000040001000ULL

static const uint8_t user_stub_code[] = {
    0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x00, /* mov rax, 0 */
    0xCD, 0x80,                               /* int 0x80 */
    0xEB, 0xFC                                /* jmp -4 (back to int 0x80) */
};

static uint64_t user_pml4_phys = 0;

void usermode_set_pml4(uint64_t pml4_phys)
{
    user_pml4_phys = pml4_phys;
}

int usermode_prepare_stub(void** entry, void** user_stack, uint64_t* rsp0_out)
{
    if (!entry || !user_stack || !rsp0_out) {
        return RDNX_E_INVALID;
    }

    extern void kputs(const char* str);
    if (bootlog_is_verbose()) {
        kputs("[USERMODE] prepare_stub\n");
    }

    user_pml4_phys = paging_create_user_pml4();
    if (!user_pml4_phys) {
        if (bootlog_is_verbose()) {
            kputs("[USERMODE] create_pml4 failed\n");
        }
        return RDNX_E_NOMEM;
    }

    uint64_t code_phys = pmm_alloc_page_in_zone(PMM_ZONE_LOW);
    uint64_t stack_phys = pmm_alloc_page_in_zone(PMM_ZONE_LOW);
    if (!code_phys || !stack_phys) {
        if (bootlog_is_verbose()) {
            kputs("[USERMODE] alloc pages failed\n");
        }
        return RDNX_E_NOMEM;
    }

    if (paging_map_page_4kb_pml4(user_pml4_phys, USER_CODE_VA, code_phys, PTE_PRESENT | PTE_USER) != 0) {
        if (bootlog_is_verbose()) {
            kputs("[USERMODE] map code failed\n");
        }
        return RDNX_E_GENERIC;
    }
    if (paging_map_page_4kb_pml4(user_pml4_phys, USER_STACK_VA, stack_phys, PTE_PRESENT | PTE_RW | PTE_USER) != 0) {
        if (bootlog_is_verbose()) {
            kputs("[USERMODE] map stack failed\n");
        }
        return RDNX_E_GENERIC;
    }

    memcpy(X86_64_PHYS_TO_VIRT(code_phys), user_stub_code, sizeof(user_stub_code));
    memset(X86_64_PHYS_TO_VIRT(stack_phys), 0, X86_64_PAGE_SIZE);
    if (bootlog_is_verbose()) {
        kputs("[USERMODE] stub mapped\n");
    }

    *entry = (void*)(uintptr_t)USER_CODE_VA;
    *user_stack = (void*)(uintptr_t)(USER_STACK_VA + X86_64_PAGE_SIZE - 16);
    *rsp0_out = 0;
    return RDNX_OK;
}

void usermode_enter(void* entry, void* user_stack, uint64_t rsp0, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    tss_set_rsp0(rsp0);
    extern void kputs(const char* str);
    if (bootlog_is_verbose()) {
        kputs("[USERMODE] switching CR3\n");
    }
    if (user_pml4_phys) {
        paging_switch_pml4(user_pml4_phys);
    }
    if (bootlog_is_verbose()) {
        kputs("[USERMODE] switched CR3\n");
        kputs("[USERMODE] about to iretq\n");
    }

    uint64_t user_cs = GDT_USER_CS | 0x3;
    uint64_t user_ds = GDT_USER_DS | 0x3;

    __asm__ volatile (
        "cli\n\t"
        "movw %w0, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %4, %%rdi\n\t"
        "mov %5, %%rsi\n\t"
        "mov %6, %%rdx\n\t"
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
          "r"(entry),
          "r"(arg0),
          "r"(arg1),
          "r"(arg2)
        : "memory", "rax", "rdi", "rsi", "rdx"
    );
}
