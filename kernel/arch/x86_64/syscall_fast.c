#include "syscall_fast.h"
#include "interrupt_frame.h"
#include "../../common/syscall.h"
#include "../../core/task.h"

extern void x86_64_syscall_fast_entry(void);
uint64_t g_syscall_user_rsp_shadow = 0;

enum {
    X86_MSR_EFER = 0xC0000080,
    X86_MSR_STAR = 0xC0000081,
    X86_MSR_LSTAR = 0xC0000082,
    X86_MSR_SFMASK = 0xC0000084,
    X86_EFER_SCE = 1u << 0,
    X86_RFLAGS_TF = 1u << 8,
    X86_RFLAGS_IF = 1u << 9,
    X86_RFLAGS_DF = 1u << 10,
    X86_RFLAGS_NT = 1u << 14,
    X86_RFLAGS_AC = 1u << 18
};

static inline uint64_t x86_rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline void x86_wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

int x86_64_syscall_fast_init(void)
{
    /*
     * STAR:
     *  - bits 47:32: kernel CS selector (SS = CS + 8 on entry)
     *  - bits 63:48: base for SYSRET user CS calculation (CS = base + 16 | 3)
     *
     * With GDT layout used in RodNIX:
     *  - kernel CS = 0x08
     *  - user   CS = 0x23, user SS = 0x1B
     * Therefore SYSRET base is 0x10:
     *   (0x10 + 16) | 3 = 0x23
     *   (0x10 +  8) | 3 = 0x1B
     */
    const uint64_t kernel_cs = 0x08u;
    const uint64_t user_sysret_base = 0x10u;
    const uint64_t star = (user_sysret_base << 48) | (kernel_cs << 32);
    const uint64_t sfmask = X86_RFLAGS_NT |
                            X86_RFLAGS_TF |
                            X86_RFLAGS_IF |
                            X86_RFLAGS_DF |
                            X86_RFLAGS_AC;

    uint64_t efer = x86_rdmsr(X86_MSR_EFER);
    efer |= X86_EFER_SCE;
    x86_wrmsr(X86_MSR_EFER, efer);
    x86_wrmsr(X86_MSR_STAR, star);
    x86_wrmsr(X86_MSR_LSTAR, (uint64_t)(uintptr_t)&x86_64_syscall_fast_entry);
    x86_wrmsr(X86_MSR_SFMASK, sfmask);
    return 0;
}

uint64_t x86_64_syscall_dispatch_frame(interrupt_frame_t* frame, int fast_entry)
{
    thread_t* cur;
    void* prev_arch;
    uint64_t ret;

    if (!frame) {
        return (uint64_t)-2;
    }

    if (fast_entry) {
        syscall_account_fast();
    } else {
        syscall_account_int80();
    }

    cur = thread_get_current();
    prev_arch = NULL;
    if (cur) {
        prev_arch = cur->arch_specific;
        cur->arch_specific = frame;
    }

    /* Keep one ABI mapping for both entries: nr=rax, a1..a3=rdi/rsi/rdx, a4..a6=r10/r8/r9. */
    ret = syscall_dispatch(frame->rax,
                           frame->rdi,
                           frame->rsi,
                           frame->rdx,
                           frame->r10,
                           frame->r8,
                           frame->r9);

    if (cur) {
        cur->arch_specific = prev_arch;
    }
    frame->rax = ret;
    return ret;
}

uint64_t x86_64_syscall_fast_dispatch_frame(interrupt_frame_t* frame)
{
    return x86_64_syscall_dispatch_frame(frame, 1);
}
