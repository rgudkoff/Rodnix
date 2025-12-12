/**
 * @file x86_64/cpu.c
 * @brief Реализация работы с процессором для x86_64
 */

#include "../../core/cpu.h"
#include "types.h"
#include <stddef.h>

static cpu_info_t cpu_info_cache;
static uint32_t cpu_count = 1;
static bool cpu_initialized = false;

int cpu_init(void)
{
    if (cpu_initialized) {
        return 0;
    }
    
    /* Получение информации о процессоре через CPUID */
    uint32_t eax, ebx, ecx, edx;
    
    /* CPUID для получения vendor string */
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(0));
    
    char vendor[13];
    *((uint32_t*)vendor) = ebx;
    *((uint32_t*)(vendor + 4)) = edx;
    *((uint32_t*)(vendor + 8)) = ecx;
    vendor[12] = '\0';
    
    cpu_info_cache.cpu_id = 0;
    cpu_info_cache.apic_id = 0;
    cpu_info_cache.vendor = "Unknown";
    cpu_info_cache.model = "x86_64";
    cpu_info_cache.features = 0;
    cpu_info_cache.cores = 1;
    cpu_info_cache.threads = 1;
    
    /* TODO: Получить полную информацию о процессоре через CPUID */
    /* TODO: Определить количество ядер и потоков */
    
    cpu_initialized = true;
    return 0;
}

int cpu_get_info(cpu_info_t* info)
{
    if (!info) {
        return -1;
    }
    
    if (!cpu_initialized) {
        cpu_init();
    }
    
    *info = cpu_info_cache;
    return 0;
}

uint32_t cpu_get_id(void)
{
    /* TODO: Получить реальный ID процессора через APIC */
    return 0;
}

uint32_t cpu_get_count(void)
{
    return cpu_count;
}

void cpu_save_context(thread_context_t* ctx)
{
    if (!ctx) return;
    
    /* Сохранение регистров в контекст */
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
    
    /* Восстановление регистров из контекста */
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
    
    /* Сохранение текущего контекста */
    cpu_save_context(from);
    
    /* Восстановление нового контекста */
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
    /* TODO: Получить частоту процессора через CPUID или MSR */
    return 0;
}

uint64_t cpu_get_time(void)
{
    uint32_t eax, edx;
    __asm__ volatile ("rdtsc" : "=a"(eax), "=d"(edx));
    return ((uint64_t)edx << 32) | eax;
}

