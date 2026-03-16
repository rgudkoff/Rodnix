#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static void load_markers(uint64_t b, uint64_t r12, uint64_t r13, uint64_t r14, uint64_t r15)
{
    __asm__ volatile (
        "mov %0, %%rbx\n\t"
        "mov %1, %%r12\n\t"
        "mov %2, %%r13\n\t"
        "mov %3, %%r14\n\t"
        "mov %4, %%r15\n\t"
        :
        : "r"(b), "r"(r12), "r"(r13), "r"(r14), "r"(r15)
        : "rbx", "r12", "r13", "r14", "r15", "memory"
    );
}

static void read_markers(uint64_t* b, uint64_t* r12, uint64_t* r13, uint64_t* r14, uint64_t* r15)
{
    __asm__ volatile (
        "mov %%rbx, %0\n\t"
        "mov %%r12, %1\n\t"
        "mov %%r13, %2\n\t"
        "mov %%r14, %3\n\t"
        "mov %%r15, %4\n\t"
        : "=r"(*b), "=r"(*r12), "=r"(*r13), "=r"(*r14), "=r"(*r15)
        :
        : "memory"
    );
}

int main(void)
{
    const uint64_t eb = 0x1111222233334444ull;
    const uint64_t e12 = 0x5555666677778888ull;
    const uint64_t e13 = 0x9999aaaabbbbccccull;
    const uint64_t e14 = 0xddddeeeeffff0001ull;
    const uint64_t e15 = 0x1234567890abcdefull;
    uint64_t ab = 0, a12 = 0, a13 = 0, a14 = 0, a15 = 0;

    load_markers(eb, e12, e13, e14, e15);

    /* Trigger syscall path without relying on stdio internals. */
    if (write(STDOUT_FILENO, "", 0) < 0) {
        perror("syscall_regs: write");
        return 1;
    }

    read_markers(&ab, &a12, &a13, &a14, &a15);

    if (ab != eb || a12 != e12 || a13 != e13 || a14 != e14 || a15 != e15) {
        fprintf(stderr,
                "syscall_regs: FAIL rbx=%p r12=%p r13=%p r14=%p r15=%p\n",
                (void*)(uintptr_t)ab,
                (void*)(uintptr_t)a12,
                (void*)(uintptr_t)a13,
                (void*)(uintptr_t)a14,
                (void*)(uintptr_t)a15);
        return 1;
    }

    printf("[syscall-regs] PASS\n");
    return 0;
}
