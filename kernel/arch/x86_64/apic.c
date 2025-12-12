/**
 * @file apic.c
 * @brief APIC (Advanced Programmable Interrupt Controller) implementation for x86_64
 * 
 * This module implements APIC initialization and management. APIC is the modern
 * interrupt controller for x86_64 systems, providing better support for multi-core
 * systems and more flexible interrupt routing.
 * 
 * @note This implementation follows XNU-style architecture but is adapted for RodNIX.
 * @note APIC is preferred over PIC on modern systems, but PIC is still required
 *       for compatibility and as a fallback.
 */

#include "types.h"
#include "config.h"
#include "../../include/debug.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * APIC Register Definitions
 * ============================================================================ */

/* Local APIC Base Address (MSR 0x1B) */
#define APIC_BASE_MSR        0x1B
#define APIC_BASE_ENABLE     (1UL << 11)
#define APIC_BASE_BSP        (1UL << 8)

/* Local APIC Registers (memory-mapped, offset from base) */
#define APIC_ID              0x020    /* Local APIC ID */
#define APIC_VERSION         0x030    /* Local APIC Version */
#define APIC_TPR             0x080    /* Task Priority Register */
#define APIC_APR             0x090    /* Arbitration Priority Register */
#define APIC_PPR             0x0A0    /* Processor Priority Register */
#define APIC_EOI             0x0B0    /* End of Interrupt */
#define APIC_SVR             0x0F0    /* Spurious Interrupt Vector Register */
#define APIC_ESR             0x280    /* Error Status Register */
#define APIC_ICR_LOW         0x300    /* Interrupt Command Register (low) */
#define APIC_ICR_HIGH        0x310    /* Interrupt Command Register (high) */
#define APIC_LVT_TIMER       0x320    /* Local Vector Table - Timer */
#define APIC_LVT_THERMAL     0x330    /* Local Vector Table - Thermal */
#define APIC_LVT_PERF        0x340    /* Local Vector Table - Performance */
#define APIC_LVT_LINT0       0x350    /* Local Vector Table - LINT0 */
#define APIC_LVT_LINT1       0x360    /* Local Vector Table - LINT1 */
#define APIC_LVT_ERROR       0x370    /* Local Vector Table - Error */
#define APIC_TIMER_INITCNT   0x380    /* Timer Initial Count */
#define APIC_TIMER_CURRCNT   0x390    /* Timer Current Count */
#define APIC_TIMER_DIV       0x3E0    /* Timer Divide Configuration */

/* APIC SVR flags */
#define APIC_SVR_ENABLE      (1 << 8)
#define APIC_SVR_SPURIOUS_VECTOR  0xFF

/* APIC LVT flags */
#define APIC_LVT_MASKED      (1 << 16)
#define APIC_LVT_TIMER_PERIODIC   (1 << 17)

/* ============================================================================
 * APIC State
 * ============================================================================ */

static volatile uint32_t* apic_base = NULL;
static bool apic_initialized = false;
static bool apic_available = false;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @function apic_read_msr
 * @brief Read Model-Specific Register (MSR)
 * 
 * @param msr MSR number
 * @return MSR value
 */
static uint64_t apic_read_msr(uint32_t msr)
{
    extern void kputs(const char* str);
    
    kputs("[APIC-RDMSR-1] Before RDMSR\n");
    __asm__ volatile ("" ::: "memory");
    
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-RDMSR-2] After RDMSR\n");
    __asm__ volatile ("" ::: "memory");
    
    uint64_t result = ((uint64_t)high << 32) | low;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-RDMSR-3] Return\n");
    __asm__ volatile ("" ::: "memory");
    return result;
}

/**
 * @function apic_write_msr
 * @brief Write Model-Specific Register (MSR)
 * 
 * @param msr MSR number
 * @param value Value to write
 */
static void apic_write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

/**
 * @function apic_read_register
 * @brief Read APIC register
 * 
 * @param offset Register offset from APIC base
 * @return Register value
 */
static uint32_t apic_read_register(uint32_t offset)
{
    extern void kputs(const char* str);
    extern void kprint_hex(uint64_t value);
    
    kputs("[APIC-REG-1] Check base\n");
    __asm__ volatile ("" ::: "memory");
    if (!apic_base) {
        kputs("[APIC-REG-1.1] Base is NULL\n");
        __asm__ volatile ("" ::: "memory");
        return 0;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-REG-2] Calculate index\n");
    __asm__ volatile ("" ::: "memory");
    uint32_t index = offset / sizeof(uint32_t);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-REG-3] Get pointer\n");
    __asm__ volatile ("" ::: "memory");
    volatile uint32_t* reg_ptr = &apic_base[index];
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-REG-4] Read register\n");
    __asm__ volatile ("" ::: "memory");
    
    /* APIC registers are memory-mapped I/O - need careful access */
    /* Use simple volatile pointer dereference (compiler will handle it) */
    kputs("[APIC-REG-4.1] Before read\n");
    __asm__ volatile ("" ::: "memory");
    
    /* Simple volatile read - compiler will generate appropriate instructions */
    /* APIC registers must be accessed as 32-bit aligned */
    uint32_t value = *reg_ptr;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-REG-4.2] After read\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-REG-5] Return\n");
    __asm__ volatile ("" ::: "memory");
    return value;
}

/**
 * @function apic_write_register
 * @brief Write APIC register
 * 
 * @param offset Register offset from APIC base
 * @param value Value to write
 */
static void apic_write_register(uint32_t offset, uint32_t value)
{
    if (!apic_base) {
        return;
    }
    apic_base[offset / sizeof(uint32_t)] = value;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
}

/**
 * @function apic_check_cpuid
 * @brief Check if APIC is available via CPUID
 * 
 * @return true if APIC is available, false otherwise
 */
static bool apic_check_cpuid(void)
{
    extern void kputs(const char* str);
    
    kputs("[APIC-CPUID-1] Before CPUID\n");
    __asm__ volatile ("" ::: "memory");
    
    uint32_t eax, ebx, ecx, edx;
    
    kputs("[APIC-CPUID-2] Execute CPUID\n");
    __asm__ volatile ("" ::: "memory");
    /* Check CPUID feature flags */
    __asm__ volatile ("cpuid"
                      : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                      : "a"(1));
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-CPUID-3] Check bit\n");
    __asm__ volatile ("" ::: "memory");
    /* Check APIC bit (bit 9 in EDX) */
    bool result = (edx & (1 << 9)) != 0;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-CPUID-4] Return\n");
    __asm__ volatile ("" ::: "memory");
    return result;
}

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function apic_is_available
 * @brief Check if APIC is available on this system
 * 
 * @return true if APIC is available, false otherwise
 */
bool apic_is_available(void)
{
    return apic_available;
}

/**
 * @function apic_init
 * @brief Initialize the Local APIC
 * 
 * This function:
 * 1. Checks if APIC is available via CPUID
 * 2. Reads APIC base address from MSR
 * 3. Maps APIC registers to virtual memory
 * 4. Enables APIC
 * 5. Configures spurious interrupt vector
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note This must be called before using APIC for interrupts.
 * @note APIC base address is typically 0xFEE00000 (physical).
 */
int apic_init(void)
{
    extern void kputs(const char* str);
    
    kputs("[APIC-1] Check CPUID\n");
    __asm__ volatile ("" ::: "memory");
    /* Check if APIC is available */
    if (!apic_check_cpuid()) {
        kputs("[APIC-1.1] APIC not available\n");
        __asm__ volatile ("" ::: "memory");
        return -1; /* APIC not available */
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-2] Set available flag\n");
    __asm__ volatile ("" ::: "memory");
    apic_available = true;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-3] Read MSR\n");
    __asm__ volatile ("" ::: "memory");
    /* Read APIC base address from MSR */
    uint64_t apic_base_msr = apic_read_msr(APIC_BASE_MSR);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-4] Extract base\n");
    __asm__ volatile ("" ::: "memory");
    /* Extract physical base address (bits 12-35) */
    uint64_t apic_base_phys = apic_base_msr & 0xFFFFF000;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-5] Enable in MSR\n");
    __asm__ volatile ("" ::: "memory");
    /* Enable APIC in MSR if not already enabled */
    if (!(apic_base_msr & APIC_BASE_ENABLE)) {
        apic_write_msr(APIC_BASE_MSR, apic_base_msr | APIC_BASE_ENABLE);
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-6] Map registers\n");
    __asm__ volatile ("" ::: "memory");
    /* Map APIC registers to virtual memory with MMIO flags (PCD) */
    /* APIC base is typically at 0xFEE00000, which needs uncached access */
    extern void kprint_hex(uint64_t value);
    kputs("[APIC-6.1] Base phys = ");
    kprint_hex(apic_base_phys);
    kputs("\n");
    __asm__ volatile ("" ::: "memory");
    
    /* APIC registers need to be mapped with PCD (Page Cache Disable) flag */
    /* This ensures uncached access for MMIO registers */
    /* Use identity mapping with PCD flag */
    extern int paging_map_page_4kb(uint64_t virt, uint64_t phys, uint64_t flags);
    extern uint64_t paging_get_physical(uint64_t virt);
    
    /* APIC region is 4KB, map it with PCD flag for uncached access */
    uint64_t apic_virt = apic_base_phys; /* Identity mapping */
    uint64_t mmio_flags = 0x001 | 0x002 | 0x010; /* PRESENT | RW | PCD (uncached) */
    
    kputs("[APIC-6.2] Map APIC with PCD flag\n");
    __asm__ volatile ("" ::: "memory");
    if (paging_map_page_4kb(apic_virt, apic_base_phys, mmio_flags) != 0) {
        kputs("[APIC-6.2.1] Failed to map APIC\n");
        __asm__ volatile ("" ::: "memory");
        return -1;
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-6.3] APIC mapped\n");
    __asm__ volatile ("" ::: "memory");
    
    apic_base = (volatile uint32_t*)apic_virt;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-6.4] Base assigned\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-7] Read SVR\n");
    __asm__ volatile ("" ::: "memory");
    /* Enable APIC (set SVR enable bit) */
    /* NOTE: This may cause page fault if APIC is not properly mapped */
    /* If it fails, we'll fallback to PIC */
    uint32_t svr;
    kputs("[APIC-7.1] Before read SVR\n");
    __asm__ volatile ("" ::: "memory");
    svr = apic_read_register(APIC_SVR);
    __asm__ volatile ("" ::: "memory");
    kputs("[APIC-7.2] After read SVR\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-8] Configure SVR\n");
    __asm__ volatile ("" ::: "memory");
    svr |= APIC_SVR_ENABLE;
    svr &= ~0xFF; /* Clear spurious vector */
    svr |= APIC_SVR_SPURIOUS_VECTOR; /* Set spurious vector to 0xFF */
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-9] Write SVR\n");
    __asm__ volatile ("" ::: "memory");
    apic_write_register(APIC_SVR, svr);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-10] Set initialized\n");
    __asm__ volatile ("" ::: "memory");
    apic_initialized = true;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-OK] Done\n");
    __asm__ volatile ("" ::: "memory");
    
    return 0;
}

/**
 * @function apic_enable
 * @brief Enable APIC
 */
void apic_enable(void)
{
    if (!apic_initialized) {
        return;
    }
    
    uint32_t svr = apic_read_register(APIC_SVR);
    svr |= APIC_SVR_ENABLE;
    apic_write_register(APIC_SVR, svr);
}

/**
 * @function apic_disable
 * @brief Disable APIC
 */
void apic_disable(void)
{
    if (!apic_initialized) {
        return;
    }
    
    uint32_t svr = apic_read_register(APIC_SVR);
    svr &= ~APIC_SVR_ENABLE;
    apic_write_register(APIC_SVR, svr);
}

/**
 * @function apic_send_eoi
 * @brief Send End of Interrupt to APIC
 * 
 * This must be called at the end of interrupt handlers when using APIC.
 */
void apic_send_eoi(void)
{
    if (!apic_initialized) {
        return;
    }
    
    apic_write_register(APIC_EOI, 0);
}

/**
 * @function apic_enable_irq
 * @brief Enable IRQ in I/O APIC (placeholder for now)
 * 
 * @param irq IRQ number
 * 
 * @note This will be implemented when I/O APIC is added
 */
void apic_enable_irq(uint8_t irq)
{
    (void)irq;
    /* TODO: Implement I/O APIC IRQ enabling */
}

/**
 * @function apic_disable_irq
 * @brief Disable IRQ in I/O APIC (placeholder for now)
 * 
 * @param irq IRQ number
 * 
 * @note This will be implemented when I/O APIC is added
 */
void apic_disable_irq(uint8_t irq)
{
    (void)irq;
    /* TODO: Implement I/O APIC IRQ disabling */
}

/* ============================================================================
 * LAPIC Timer Functions
 * ============================================================================ */

/**
 * @function apic_timer_init
 * @brief Initialize LAPIC timer
 * 
 * @param frequency Desired timer frequency in Hz
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note LAPIC timer is per-CPU and more accurate than PIT
 */
int apic_timer_init(uint32_t frequency)
{
    if (!apic_initialized) {
        return -1;
    }
    
    /* Calculate timer divisor and initial count */
    /* LAPIC timer runs at bus frequency, typically ~100MHz */
    /* We need to calibrate it first, but for now use a simple approach */
    
    /* Set timer to one-shot mode first for calibration */
    uint32_t lvt_timer = apic_read_register(APIC_LVT_TIMER);
    lvt_timer &= ~APIC_LVT_TIMER_PERIODIC; /* One-shot mode */
    lvt_timer &= ~APIC_LVT_MASKED; /* Unmask timer */
    lvt_timer |= 32; /* Timer interrupt vector (IRQ 0 -> vector 32) */
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
    
    /* Set timer divide configuration (divide by 1) */
    apic_write_register(APIC_TIMER_DIV, 0x0B); /* Divide by 1 */
    
    /* TODO: Calibrate timer using PIT as reference */
    /* For now, use a simple initial count */
    uint32_t initial_count = 1000000; /* Approximate for 100Hz */
    apic_write_register(APIC_TIMER_INITCNT, initial_count);
    
    return 0;
}

/**
 * @function apic_timer_start
 * @brief Start LAPIC timer in periodic mode
 */
void apic_timer_start(void)
{
    if (!apic_initialized) {
        return;
    }
    
    /* Set timer to periodic mode */
    uint32_t lvt_timer = apic_read_register(APIC_LVT_TIMER);
    lvt_timer |= APIC_LVT_TIMER_PERIODIC; /* Periodic mode */
    lvt_timer &= ~APIC_LVT_MASKED; /* Unmask timer */
    lvt_timer |= 32; /* Timer interrupt vector */
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
    
    /* Set initial count to start timer */
    uint32_t initial_count = 1000000; /* Approximate for 100Hz */
    apic_write_register(APIC_TIMER_INITCNT, initial_count);
}

/**
 * @function apic_timer_stop
 * @brief Stop LAPIC timer
 */
void apic_timer_stop(void)
{
    if (!apic_initialized) {
        return;
    }
    
    /* Mask timer */
    uint32_t lvt_timer = apic_read_register(APIC_LVT_TIMER);
    lvt_timer |= APIC_LVT_MASKED;
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
}

/**
 * @function apic_timer_get_ticks
 * @brief Get current timer count
 * 
 * @return Current timer count
 */
uint32_t apic_timer_get_ticks(void)
{
    if (!apic_initialized) {
        return 0;
    }
    
    return apic_read_register(APIC_TIMER_CURRCNT);
}

