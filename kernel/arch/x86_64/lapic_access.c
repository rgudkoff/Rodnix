#include "lapic_access.h"
#include "lapic_regs.h"
#include "config.h"
#include "paging.h"
#include <stddef.h>
#include <stdint.h>

static volatile uint32_t* g_lapic_mmio = NULL;
static lapic_mode_t g_lapic_mode = LAPIC_MODE_NONE;
#define LAPIC_MMIO_VIRT 0xFFFFFFFFFEE00000ULL

static inline uint64_t lapic_rdmsr(uint32_t msr)
{
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline void lapic_wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static bool lapic_cpu_has_x2apic(void)
{
    uint32_t eax = 1;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    (void)ebx;
    (void)edx;
    return (ecx & (1u << 21)) != 0;
}

int lapic_access_init(uint64_t apic_phys, bool prefer_x2apic)
{
    uint64_t apic_base = lapic_rdmsr(APIC_BASE_MSR);

    if (prefer_x2apic && lapic_cpu_has_x2apic()) {
        apic_base |= APIC_BASE_ENABLE | APIC_BASE_X2APIC;
        lapic_wrmsr(APIC_BASE_MSR, apic_base);
        g_lapic_mode = LAPIC_MODE_X2APIC;
        g_lapic_mmio = NULL;
        return 0;
    }

    apic_base |= APIC_BASE_ENABLE;
    apic_base &= ~APIC_BASE_X2APIC;
    lapic_wrmsr(APIC_BASE_MSR, apic_base);

    {
        uint64_t apic_virt = LAPIC_MMIO_VIRT;
        uint64_t mmio_flags = 0x001 | 0x002 | 0x010; /* PRESENT | RW | PCD */
        if (paging_map_page_4kb(apic_virt, apic_phys, mmio_flags) != 0) {
            g_lapic_mode = LAPIC_MODE_NONE;
            g_lapic_mmio = NULL;
            return -1;
        }
        g_lapic_mmio = (volatile uint32_t*)apic_virt;
    }

    g_lapic_mode = LAPIC_MODE_XAPIC;
    return 0;
}

bool lapic_access_ready(void)
{
    if (g_lapic_mode == LAPIC_MODE_X2APIC) {
        return true;
    }
    if (g_lapic_mode == LAPIC_MODE_XAPIC) {
        return g_lapic_mmio != NULL;
    }
    return false;
}

lapic_mode_t lapic_access_mode(void)
{
    return g_lapic_mode;
}

const char* lapic_access_mode_name(void)
{
    if (g_lapic_mode == LAPIC_MODE_X2APIC) {
        return "x2apic";
    }
    if (g_lapic_mode == LAPIC_MODE_XAPIC) {
        return "xapic";
    }
    return "none";
}

uint32_t lapic_access_read(uint32_t reg_off)
{
    if (g_lapic_mode == LAPIC_MODE_X2APIC) {
        uint32_t msr = X2APIC_MSR_BASE + (reg_off >> 4);
        return (uint32_t)lapic_rdmsr(msr);
    }
    if (g_lapic_mode == LAPIC_MODE_XAPIC && g_lapic_mmio) {
        return g_lapic_mmio[reg_off >> 2];
    }
    return 0;
}

void lapic_access_write(uint32_t reg_off, uint32_t value)
{
    if (g_lapic_mode == LAPIC_MODE_X2APIC) {
        uint32_t msr = X2APIC_MSR_BASE + (reg_off >> 4);
        __asm__ volatile ("mfence\n\tlfence" ::: "memory");
        lapic_wrmsr(msr, (uint64_t)value);
        return;
    }
    if (g_lapic_mode == LAPIC_MODE_XAPIC && g_lapic_mmio) {
        g_lapic_mmio[reg_off >> 2] = value;
        __asm__ volatile ("" ::: "memory");
    }
}
