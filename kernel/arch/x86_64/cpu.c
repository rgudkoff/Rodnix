/**
 * @file x86_64/cpu.c
 * @brief CPU implementation for x86_64
 */

#include "../../core/cpu.h"
#include "types.h"
#include <stddef.h>

/* Use volatile to prevent compiler optimizations that might cause issues */
static volatile cpu_info_t cpu_info_cache;
static volatile uint32_t cpu_count = 1;
static volatile bool cpu_initialized = false;

int cpu_init(void)
{
    /* Use volatile read to prevent optimization issues */
    volatile bool initialized = cpu_initialized;
    __asm__ volatile ("" ::: "memory");
    
    if (initialized) {
        return 0;
    }
    
    /* Get CPU information via CPUID */
    /* Use memory barriers to ensure proper ordering */
    uint32_t eax, ebx, ecx, edx;
    
    __asm__ volatile ("" ::: "memory");
    /* CPUID to get vendor string */
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(0));
    __asm__ volatile ("" ::: "memory");
    
    char vendor[13];
    *((uint32_t*)vendor) = ebx;
    *((uint32_t*)(vendor + 4)) = edx;
    *((uint32_t*)(vendor + 8)) = ecx;
    vendor[12] = '\0';
    __asm__ volatile ("" ::: "memory");
    
    /* Fill CPU info cache */
    /* Use memory barriers between assignments */
    cpu_info_cache.cpu_id = 0;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.apic_id = 0;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.vendor = "Unknown";
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.model = "x86_64";
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.features = 0;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.cores = 1;
    __asm__ volatile ("" ::: "memory");
    cpu_info_cache.threads = 1;
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
    if (!ctx) return;
    
    /* Save registers to context */
    __asm__ volatile (
        "mov %%rsp, %0\n\t"
        "mov $., %1\n\t"
        : "=r"(ctx->stack_pointer), "=r"(ctx->program_counter)
        :
        : "memory"
    );
}

void cpu_restore_context(thread_context_t* ctx)
{
    if (!ctx) return;
    
    /* Restore registers from context */
    __asm__ volatile (
        "mov %0, %%rsp\n\t"
        "jmp *%1\n\t"
        :
        : "r"(ctx->stack_pointer), "r"(ctx->program_counter)
        : "memory"
    );
}

void cpu_switch_thread(thread_context_t* from, thread_context_t* to)
{
    if (!from || !to) return;
    
    /* Save current context */
    cpu_save_context(from);
    
    /* Restore new context */
    cpu_restore_context(to);
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
    /* TODO: Get CPU frequency via CPUID or MSR */
    return 0;
}

uint64_t cpu_get_time(void)
{
    uint32_t eax, edx;
    __asm__ volatile ("rdtsc" : "=a"(eax), "=d"(edx));
    return ((uint64_t)edx << 32) | eax;
}

