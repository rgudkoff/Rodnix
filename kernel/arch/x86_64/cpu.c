/**
 * @file x86_64/cpu.c
 * @brief CPU implementation for x86_64
 */

#include "../../core/cpu.h"
#include "types.h"
#include "gdt.h"
#include "../../../include/common.h"
#include <stddef.h>

/* Use volatile to prevent compiler optimizations that might cause issues */
static volatile cpu_info_t cpu_info_cache;
static volatile uint32_t cpu_count = 1;
static volatile bool cpu_initialized = false;
static volatile uint64_t cpu_freq_hz = 0;
static char cpu_vendor_str[16];
static char cpu_brand_str[64];

static inline void cpuid_exec(uint32_t leaf, uint32_t subleaf,
                              uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx)
{
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid"
                      : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                      : "a"(leaf), "c"(subleaf));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static uint32_t cpu_extract_display_family(uint32_t eax1)
{
    uint32_t family = (eax1 >> 8) & 0xF;
    uint32_t ext_family = (eax1 >> 20) & 0xFF;
    if (family == 0xF) {
        family += ext_family;
    }
    return family;
}

static uint32_t cpu_extract_display_model(uint32_t eax1)
{
    uint32_t family = (eax1 >> 8) & 0xF;
    uint32_t model = (eax1 >> 4) & 0xF;
    uint32_t ext_model = (eax1 >> 16) & 0xF;
    if (family == 0x6 || family == 0xF) {
        model |= (ext_model << 4);
    }
    return model;
}

int cpu_init(void)
{
    /* Use volatile read to prevent optimization issues */
    volatile bool initialized = cpu_initialized;
    __asm__ volatile ("" ::: "memory");
    
    if (initialized) {
        return 0;
    }

    /* Enable SSE/SSE2 for compiler-generated XMM instructions */
    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);  /* Clear EM (x87 emulation) */
    cr0 |= (1ULL << 1);   /* Set MP (monitor co-processor) */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);   /* OSFXSR */
    cr4 |= (1ULL << 10);  /* OSXMMEXCPT */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));

    /* Initialize GDT/TSS (user segments + RSP0) */
    gdt_init();
    
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    uint32_t max_basic_leaf = 0;
    uint32_t max_ext_leaf = 0;

    memset(cpu_vendor_str, 0, sizeof(cpu_vendor_str));
    memset(cpu_brand_str, 0, sizeof(cpu_brand_str));

    cpuid_exec(0, 0, &max_basic_leaf, &ebx, &ecx, &edx);
    ((uint32_t*)cpu_vendor_str)[0] = ebx;
    ((uint32_t*)cpu_vendor_str)[1] = edx;
    ((uint32_t*)cpu_vendor_str)[2] = ecx;
    cpu_vendor_str[12] = '\0';

    cpuid_exec(0x80000000u, 0, &max_ext_leaf, NULL, NULL, NULL);
    if (max_ext_leaf >= 0x80000004u) {
        uint32_t* p = (uint32_t*)cpu_brand_str;
        for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; leaf++) {
            cpuid_exec(leaf, 0, &eax, &ebx, &ecx, &edx);
            *p++ = eax;
            *p++ = ebx;
            *p++ = ecx;
            *p++ = edx;
        }
        cpu_brand_str[48] = '\0';
        uint32_t i = 0;
        while (cpu_brand_str[i] == ' ' && cpu_brand_str[i] != '\0') {
            i++;
        }
        if (i > 0) {
            memmove(cpu_brand_str, cpu_brand_str + i, sizeof(cpu_brand_str) - i);
            cpu_brand_str[sizeof(cpu_brand_str) - 1] = '\0';
        }
    }

    uint32_t eax1 = 0;
    uint32_t ebx1 = 0;
    uint32_t ecx1 = 0;
    uint32_t edx1 = 0;
    if (max_basic_leaf >= 1) {
        cpuid_exec(1, 0, &eax1, &ebx1, &ecx1, &edx1);
    }

    uint32_t ebx7 = 0;
    uint32_t ecx7 = 0;
    if (max_basic_leaf >= 7) {
        cpuid_exec(7, 0, NULL, &ebx7, &ecx7, NULL);
    }

    uint32_t threads = 1;
    uint32_t cores = 1;
    if (max_basic_leaf >= 0x0Bu) {
        uint32_t smt_count = 0;
        uint32_t pkg_count = 0;
        for (uint32_t lvl = 0; lvl < 8; lvl++) {
            uint32_t leax = 0, lebx = 0, lecx = 0, ledx = 0;
            cpuid_exec(0x0Bu, lvl, &leax, &lebx, &lecx, &ledx);
            uint32_t level_type = (lecx >> 8) & 0xFFu;
            if (level_type == 0 || lebx == 0) {
                break;
            }
            if (level_type == 1) {
                smt_count = lebx & 0xFFFFu;
            } else if (level_type == 2) {
                pkg_count = lebx & 0xFFFFu;
            }
        }
        if (smt_count > 0) {
            threads = smt_count;
        }
        if (pkg_count >= threads && threads > 0) {
            cores = pkg_count / threads;
            if (cores == 0) {
                cores = 1;
            }
        }
    } else {
        uint32_t logical = (ebx1 >> 16) & 0xFFu;
        if (logical > 0) {
            threads = logical;
        }
        if (max_basic_leaf >= 4) {
            uint32_t eax4 = 0;
            cpuid_exec(4, 0, &eax4, NULL, NULL, NULL);
            uint32_t c = ((eax4 >> 26) & 0x3Fu) + 1u;
            if (c > 0) {
                cores = c;
            }
        }
    }

    uint64_t freq_hz = 0;
    if (max_basic_leaf >= 0x16u) {
        uint32_t f16_eax = 0;
        cpuid_exec(0x16u, 0, &f16_eax, NULL, NULL, NULL);
        if (f16_eax != 0) {
            freq_hz = (uint64_t)f16_eax * 1000000ULL; /* MHz -> Hz */
        }
    }
    if (freq_hz == 0 && max_basic_leaf >= 0x15u) {
        uint32_t f15_eax = 0, f15_ebx = 0, f15_ecx = 0;
        cpuid_exec(0x15u, 0, &f15_eax, &f15_ebx, &f15_ecx, NULL);
        if (f15_eax != 0 && f15_ebx != 0 && f15_ecx != 0) {
            freq_hz = ((uint64_t)f15_ecx * (uint64_t)f15_ebx) / (uint64_t)f15_eax;
        }
    }
    cpu_freq_hz = freq_hz;

    cpu_info_cache.cpu_id = 0;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.apic_id = (ebx1 >> 24) & 0xFFu;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.family = cpu_extract_display_family(eax1);
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.model_id = cpu_extract_display_model(eax1);
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.stepping = eax1 & 0xFu;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.vendor = (cpu_vendor_str[0] != '\0') ? cpu_vendor_str : "Unknown";
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.model = (cpu_brand_str[0] != '\0') ? cpu_brand_str : "x86_64";
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.features = edx1;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.features_edx = edx1;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.features_ecx = ecx1;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.ext_features_ebx = ebx7;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.ext_features_ecx = ecx7;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.cores = cores;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.threads = threads;
    __asm__ volatile ("" ::: "memory");
    
    /* Set initialized flag last, with memory barrier */
    cpu_initialized = true;
    __asm__ volatile ("" ::: "memory");
    
    return 0;
}

int cpu_get_info(cpu_info_t* info)
{
    if (!info) {
        return -1;
    }
    
    /* Use volatile read to prevent optimization issues */
    volatile bool initialized = cpu_initialized;
    __asm__ volatile ("" ::: "memory");
    
    if (!initialized) {
        cpu_init();
    }
    
    /* Copy cache with memory barrier */
    __asm__ volatile ("" ::: "memory");
    *info = *(cpu_info_t*)&cpu_info_cache;
    __asm__ volatile ("" ::: "memory");
    
    return 0;
}

uint32_t cpu_get_id(void)
{
    /* TODO: Get real CPU ID via APIC */
    return 0;
}

uint32_t cpu_get_count(void)
{
    return cpu_count;
}

void cpu_save_context(thread_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    __asm__ volatile (
        "mov %%rsp, 8(%0)\n\t"
        :
        : "r"(ctx)
        : "memory"
    );
}

void cpu_restore_context(thread_context_t* ctx)
{
    if (!ctx) {
        return;
    }

    __asm__ volatile (
        "mov 8(%0), %%rsp\n\t"
        "pop %%r15\n\t"
        "pop %%r14\n\t"
        "pop %%r13\n\t"
        "pop %%r12\n\t"
        "pop %%rbp\n\t"
        "pop %%rbx\n\t"
        "ret\n\t"
        :
        : "r"(ctx)
        : "memory"
    );
}

__attribute__((naked)) void cpu_switch_thread(thread_context_t*, thread_context_t*)
{
    __asm__ volatile (
        "push %rbx\n\t"
        "push %rbp\n\t"
        "push %r12\n\t"
        "push %r13\n\t"
        "push %r14\n\t"
        "push %r15\n\t"
        "mov %rsp, 8(%rdi)\n\t"
        "mov 8(%rsi), %rsp\n\t"
        "pop %r15\n\t"
        "pop %r14\n\t"
        "pop %r13\n\t"
        "pop %r12\n\t"
        "pop %rbp\n\t"
        "pop %rbx\n\t"
        "ret\n\t"
    );
}

void cpu_memory_barrier(void)
{
    __asm__ volatile ("mfence" ::: "memory");
}

void cpu_read_barrier(void)
{
    __asm__ volatile ("lfence" ::: "memory");
}

void cpu_write_barrier(void)
{
    __asm__ volatile ("sfence" ::: "memory");
}

uint64_t cpu_atomic_add(volatile uint64_t* ptr, uint64_t value)
{
    return __sync_add_and_fetch(ptr, value);
}

uint64_t cpu_atomic_sub(volatile uint64_t* ptr, uint64_t value)
{
    return __sync_sub_and_fetch(ptr, value);
}

uint64_t cpu_atomic_and(volatile uint64_t* ptr, uint64_t value)
{
    return __sync_and_and_fetch(ptr, value);
}

uint64_t cpu_atomic_or(volatile uint64_t* ptr, uint64_t value)
{
    return __sync_or_and_fetch(ptr, value);
}

uint64_t cpu_atomic_xor(volatile uint64_t* ptr, uint64_t value)
{
    return __sync_xor_and_fetch(ptr, value);
}

uint64_t cpu_atomic_swap(volatile uint64_t* ptr, uint64_t new_value)
{
    return __sync_lock_test_and_set(ptr, new_value);
}

uint64_t cpu_atomic_compare_and_swap(volatile uint64_t* ptr, uint64_t expected, uint64_t new_value)
{
    return __sync_val_compare_and_swap(ptr, expected, new_value);
}

void cpu_pause(void)
{
    __asm__ volatile ("pause");
}

void cpu_idle(void)
{
    __asm__ volatile ("hlt");
}

uint64_t cpu_get_frequency(void)
{
    return cpu_freq_hz;
}

uint64_t cpu_get_time(void)
{
    uint32_t eax, edx;
    __asm__ volatile ("rdtsc" : "=a"(eax), "=d"(edx));
    return ((uint64_t)edx << 32) | eax;
}
