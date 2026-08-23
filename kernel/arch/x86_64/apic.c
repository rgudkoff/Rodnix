/**
 * @file apic.c
 * @brief APIC (Advanced Programmable Interrupt Controller) implementation for x86_64
 * 
 * This module implements APIC initialization and management. APIC is the modern
 * interrupt controller for x86_64 systems, providing better support for multi-core
 * systems and more flexible interrupt routing.
 * 
 * @note This implementation is adapted for RodNIX.
 * @note APIC is preferred over PIC on modern systems, but PIC is still required
 *       for compatibility and as a fallback.
 */

#include "types.h"
#include "config.h"
#include "paging.h"
#include "pic.h"
#include "lapic_regs.h"
#include "vectors.h"
#include "lapic_access.h"
#include "acpi.h"
#include "../../../include/debug.h"
#include "../../core/interrupts.h"
#include "../../../sched/scheduler.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef APIC_DEBUG
#define APIC_DEBUG 0
#endif

/* ============================================================================
 * APIC Register Definitions
 * ============================================================================ */

/* ============================================================================
 * I/O APIC Register Definitions
 * ============================================================================ */

/* I/O APIC Base Address (default, will be overridden from ACPI MADT if available) */
#define IOAPIC_BASE_ADDR_DEFAULT   0xFEC00000
#define IOAPIC_MMIO_VIRT           0xFFFFFFFFFEC00000ULL

/* I/O APIC Registers (memory-mapped) */
#define IOAPIC_REGSEL         0x00    /* Register Select */
#define IOAPIC_REGWIN         0x10    /* Register Window */

/* I/O APIC Register Indices */
#define IOAPIC_ID             0x00    /* I/O APIC ID */
#define IOAPIC_VER             0x01    /* I/O APIC Version */
#define IOAPIC_ARB             0x02    /* Arbitration ID */
#define IOAPIC_REDIR_TBL(n)    (0x10 + (n) * 2)  /* Redirection Table Entry n (low) */
#define IOAPIC_REDIR_TBL_H(n)  (0x11 + (n) * 2)  /* Redirection Table Entry n (high) */

/* Redirection Table Entry (RTE) flags */
#define IOAPIC_RTE_VECTOR(v)        ((v) & 0xFF)
#define IOAPIC_RTE_DELIVERY_FIXED   (0UL << 8)   /* Fixed delivery mode */
#define IOAPIC_RTE_DELIVERY_LOWEST  (1UL << 8)   /* Lowest priority */
#define IOAPIC_RTE_DELIVERY_SMI     (2UL << 8)   /* SMI */
#define IOAPIC_RTE_DELIVERY_NMI     (4UL << 8)   /* NMI */
#define IOAPIC_RTE_DELIVERY_INIT    (5UL << 8)   /* INIT */
#define IOAPIC_RTE_DELIVERY_EXTINT  (7UL << 8)   /* External interrupt */
#define IOAPIC_RTE_DEST_MODE_PHYS   (0UL << 11)  /* Physical destination mode */
#define IOAPIC_RTE_DEST_MODE_LOG    (1UL << 11)  /* Logical destination mode */
#define IOAPIC_RTE_POLARITY_LOW     (0UL << 13)  /* Active low */
#define IOAPIC_RTE_POLARITY_HIGH    (1UL << 13)  /* Active high */
#define IOAPIC_RTE_TRIGGER_EDGE     (0UL << 15)  /* Edge triggered */
#define IOAPIC_RTE_TRIGGER_LEVEL    (1UL << 15)  /* Level triggered */
#define IOAPIC_RTE_MASKED           (1UL << 16)  /* Mask interrupt */
#define IOAPIC_RTE_DEST_APIC_ID(id) ((id) << 24) /* Destination APIC ID */

/* Maximum number of I/O APIC redirection entries */
#define IOAPIC_MAX_REDIR       24

/* ============================================================================
 * APIC State
 * ============================================================================ */

static bool apic_initialized = false;
static bool apic_available = false;

/* I/O APIC State */
static volatile uint32_t* ioapic_base = NULL;
static bool ioapic_initialized = false;
static bool ioapic_available = false;
static uint8_t ioapic_id = 0;
static uint8_t ioapic_version = 0;
static uint8_t ioapic_max_redir = 0;
static uint64_t ioapic_base_addr = IOAPIC_BASE_ADDR_DEFAULT;  /* Will be set from MADT */

/* LAPIC Timer State */
static uint32_t apic_timer_ticks_per_ms = 0;     /* Calibrated LAPIC ticks per ms (periodic mode) */
static uint32_t apic_timer_frequency = 0;         /* Target frequency, Hz */
static volatile uint32_t apic_timer_ticks = 0;    /* System tick counter */
static bool     apic_timer_tsc_deadline = false;   /* Using TSC-Deadline mode */
static uint64_t apic_timer_tsc_per_period = 0;    /* TSC ticks per timer period */

static inline uint64_t rdtsc64(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr64(uint32_t msr, uint64_t value)
{
    __asm__ volatile ("wrmsr"
                      :: "c"(msr),
                         "a"((uint32_t)(value & 0xFFFFFFFFu)),
                         "d"((uint32_t)(value >> 32))
                      : "memory");
}

static inline bool apic_tsc_deadline_supported(void)
{
    uint32_t ecx = 0;
    __asm__ volatile ("cpuid" : "=c"(ecx) : "a"(1u) : "ebx", "edx");
    return (ecx & CPUID_ECX_TSC_DEADLINE) != 0u;
}

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/* Forward declarations */
static int ioapic_init(void);
uint8_t apic_get_lapic_id(void);

static uint32_t ioapic_polarity_from_iso_flags(uint16_t flags)
{
    uint16_t pol = (uint16_t)(flags & 0x3u);
    if (pol == 0x3u) {
        return IOAPIC_RTE_POLARITY_LOW;
    }
    return IOAPIC_RTE_POLARITY_HIGH;
}

static uint32_t ioapic_trigger_from_iso_flags(uint16_t flags)
{
    uint16_t trg = (uint16_t)((flags >> 2) & 0x3u);
    if (trg == 0x3u) {
        return IOAPIC_RTE_TRIGGER_LEVEL;
    }
    return IOAPIC_RTE_TRIGGER_EDGE;
}

static int ioapic_route_for_legacy_irq(uint8_t irq,
                                       uint8_t* out_gsi,
                                       uint32_t* out_polarity_flag,
                                       uint32_t* out_trigger_flag)
{
    if (!out_gsi) {
        return -1;
    }

    *out_gsi = irq;
    if (out_polarity_flag) {
        *out_polarity_flag = IOAPIC_RTE_POLARITY_HIGH;
    }
    if (out_trigger_flag) {
        *out_trigger_flag = IOAPIC_RTE_TRIGGER_EDGE;
    }

    struct acpi_madt_iso_info iso;
    if (acpi_madt_get_iso_for_source(irq, &iso) == 0) {
        if (iso.gsi <= 0xFFu) {
            *out_gsi = (uint8_t)iso.gsi;
            if (out_polarity_flag) {
                *out_polarity_flag = ioapic_polarity_from_iso_flags(iso.flags);
            }
            if (out_trigger_flag) {
                *out_trigger_flag = ioapic_trigger_from_iso_flags(iso.flags);
            }
            return 0;
        }
        return -1;
    }

    return 0;
}

#if APIC_DEBUG
static const char* ioapic_polarity_name(uint32_t polarity)
{
    return polarity == IOAPIC_RTE_POLARITY_LOW ? "low" : "high";
}

static const char* ioapic_trigger_name(uint32_t trigger)
{
    return trigger == IOAPIC_RTE_TRIGGER_LEVEL ? "level" : "edge";
}

static void ioapic_log_iso_routes(void)
{
    for (uint8_t irq = 0; irq < 16; irq++) {
        struct acpi_madt_iso_info iso;
        if (acpi_madt_get_iso_for_source(irq, &iso) != 0) {
            continue;
        }

        uint32_t pol = ioapic_polarity_from_iso_flags(iso.flags);
        uint32_t trg = ioapic_trigger_from_iso_flags(iso.flags);
        kprintf("[IOAPIC-ISO] IRQ%u -> GSI%u bus=%u flags=0x%x (%s,%s)\n",
                (unsigned)irq,
                (unsigned)iso.gsi,
                (unsigned)iso.bus,
                (unsigned)iso.flags,
                ioapic_polarity_name(pol),
                ioapic_trigger_name(trg));
    }
}
#endif

/* Find I/O APIC address from ACPI MADT */
static uint64_t find_ioapic_from_madt(void)
{
    struct acpi_madt_ioapic_info info;
    if (acpi_madt_get_ioapic(0, &info) == 0) {
        return (uint64_t)info.address;
    }
    return 0;
}

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
    
    #if APIC_DEBUG
    kputs("[APIC-RDMSR-1] Before RDMSR\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
    __asm__ volatile ("" ::: "memory");
    
    #if APIC_DEBUG
    kputs("[APIC-RDMSR-2] After RDMSR\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    
    uint64_t result = ((uint64_t)high << 32) | low;
    __asm__ volatile ("" ::: "memory");
    
    #if APIC_DEBUG
    kputs("[APIC-RDMSR-3] Return\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    return result;
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
    return lapic_access_read(offset);
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
    lapic_access_write(offset, value);
}

static void apic_reset_local(void)
{
    if (!lapic_access_ready()) {
        return;
    }
    /* Mask all LVT entries before enabling APIC */
    apic_write_register(APIC_LVT_TIMER, APIC_LVT_MASKED);
    apic_write_register(APIC_LVT_THERMAL, APIC_LVT_MASKED);
    apic_write_register(APIC_LVT_PERF, APIC_LVT_MASKED);
    apic_write_register(APIC_LVT_LINT0, APIC_LVT_MASKED);
    apic_write_register(APIC_LVT_LINT1, APIC_LVT_MASKED);
    apic_write_register(APIC_LVT_ERROR, APIC_LVT_MASKED);

    /* Clear Error Status Register (write twice per Intel recommendation) */
    apic_write_register(APIC_ESR, 0);
    apic_write_register(APIC_ESR, 0);

    /* Allow all priorities */
    apic_write_register(APIC_TPR, 0);
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
    
    #if APIC_DEBUG
    #if APIC_DEBUG
    kputs("[APIC-CPUID-1] Before CPUID\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    #endif
    
    uint32_t eax, ebx, ecx, edx;
    
    #if APIC_DEBUG
    #if APIC_DEBUG
    kputs("[APIC-CPUID-2] Execute CPUID\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    #endif
    /* Check CPUID feature flags */
    __asm__ volatile ("cpuid"
                      : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                      : "a" (1));
    
    #if APIC_DEBUG
    #if APIC_DEBUG
    kputs("[APIC-CPUID-3] Check bit\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    #endif
    /* Check APIC bit (bit 9 in EDX) */
    bool result = (edx & (1 << 9)) != 0;
    __asm__ volatile ("" ::: "memory");
    
    #if APIC_DEBUG
    #if APIC_DEBUG
    kputs("[APIC-CPUID-4] Return\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    #endif
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
 * @function ioapic_is_available
 * @brief Check if I/O APIC is available
 * 
 * @return true if I/O APIC is available, false otherwise
 */
bool ioapic_is_available(void)
{
    return ioapic_available;
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
    /* Trust platform firmware and the virtual machine monitor enough to try
     * LAPIC initialization even if CPUID does not advertise APIC support.
     * Some virtualized environments get this flag wrong; CPUID is log-only. */
    bool cpuid_has_apic = apic_check_cpuid();
    if (!cpuid_has_apic) {
        kputs("[APIC-1.1] WARNING: CPUID reports no APIC, forcing APIC init (QEMU/firmware quirk?)\n");
        __asm__ volatile ("" ::: "memory");
    } else {
        kputs("[APIC-1.2] CPUID reports APIC present\n");
        __asm__ volatile ("" ::: "memory");
    }
    
    kputs("[APIC-2] APIC availability pending init\n");
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-3] Read MSR\n");
    __asm__ volatile ("" ::: "memory");
    uint64_t apic_base_msr = apic_read_msr(APIC_BASE_MSR);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-4] Extract base\n");
    __asm__ volatile ("" ::: "memory");
    uint64_t apic_base_phys = apic_base_msr & 0xFFFFF000;
    uint32_t madt_lapic_addr = 0;
    if (acpi_madt_get_lapic_addr(&madt_lapic_addr) == 0 && madt_lapic_addr != 0) {
        uint64_t madt_base = ((uint64_t)madt_lapic_addr) & 0xFFFFF000ULL;
        if (madt_base != apic_base_phys) {
            kprintf("[APIC-4.1] MADT LAPIC base=%llX differs from MSR=%llX (using MSR)\n",
                    (unsigned long long)madt_base,
                    (unsigned long long)apic_base_phys);
        } else {
            kprintf("[APIC-4.1] MADT LAPIC base matches MSR=%llX\n",
                    (unsigned long long)apic_base_phys);
        }
    }
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-5] Setup LAPIC access backend\n");
    kprintf("[APIC-5.1] Base phys = 0x%llx\n", (unsigned long long)apic_base_phys);
    __asm__ volatile ("" ::: "memory");

    if (lapic_access_init(apic_base_phys, true) != 0) {
        kputs("[APIC-5.2] Failed to initialize LAPIC access backend\n");
        __asm__ volatile ("" ::: "memory");
        return -1;
    }

    apic_available = true;
    kprintf("[APIC-5.3] LAPIC access mode: %s\n", lapic_access_mode_name());
    __asm__ volatile ("" ::: "memory");

    kputs("[APIC-6] Reset LAPIC state\n");
    __asm__ volatile ("" ::: "memory");
    apic_reset_local();
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

    {
        uint32_t ver = apic_read_register(APIC_VERSION);
        kprintf("[APIC-7.3] LAPIC version=%x svr=%x\n", ver, svr);
    }
    
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

    /* Keep legacy ExtINT path enabled until I/O APIC verdict is known. */
    kputs("[APIC-9.1] Configure LINT0 for temporary ExtINT fallback\n");
    __asm__ volatile ("" ::: "memory");
    apic_write_register(APIC_LVT_LINT0, (0x7u << 8));
    apic_write_register(APIC_LVT_LINT1, (1u << 16));
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-10] Set initialized\n");
    __asm__ volatile ("" ::: "memory");
    apic_initialized = true;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[APIC-11] Init I/O APIC\n");
    __asm__ volatile ("" ::: "memory");
    
    /* Find I/O APIC address from ACPI MADT */
    kputs("[APIC-11.0] Searching for I/O APIC in ACPI MADT...\n");
    __asm__ volatile ("" ::: "memory");
    uint64_t ioapic_addr = find_ioapic_from_madt();
    if (ioapic_addr != 0) {
        ioapic_base_addr = ioapic_addr;
        kprintf("[APIC-11.0.1] Found I/O APIC at %llX (from MADT)\n", 
                (unsigned long long)ioapic_addr);
    } else {
        kputs("[APIC-11.0.2] I/O APIC not found in MADT, using default 0xFEC00000\n");
        ioapic_base_addr = IOAPIC_BASE_ADDR_DEFAULT;
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Initialize I/O APIC for external IRQ routing */
    int ioapic_result = ioapic_init();
    if (ioapic_result == 0) {
        kputs("[APIC-11.1] I/O APIC initialized successfully\n");
        kputs("[APIC-11.2] Switching external IRQ routing to I/O APIC\n");
        /* Stop accepting ExtINT from 8259 via LAPIC LINT0. */
        apic_write_register(APIC_LVT_LINT0, APIC_LVT_MASKED);
        /* Route legacy IRQs away from PIC and fully mask PIC lines. */
        pic_set_imcr(true);
        pic_disable();
        kputs("[APIC-11.3] PIC masked and detached (APIC/IOAPIC mode)\n");
        __asm__ volatile ("" ::: "memory");
    } else {
        kputs("[APIC-11.2] I/O APIC init failed (error code above)\n");
        kputs("[APIC-11.3] Will use PIC for external IRQ routing\n");
        kputs("[APIC-11.4] Check [IOAPIC-*] logs above for failure details\n");
        /* Route external IRQs to PIC (IMCR PIC mode) */
        pic_set_imcr(false);
        /* Keep ExtINT path on LINT0 for PIC fallback mode. */
        __asm__ volatile ("" ::: "memory");
    }
    
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

/* ============================================================================
 * I/O APIC Helper Functions
 * ============================================================================ */

/**
 * @function ioapic_read_register
 * @brief Read I/O APIC register
 * 
 * @param reg Register index
 * @return Register value
 */
static uint32_t ioapic_read_register(uint8_t reg)
{
    if (!ioapic_base) {
        return 0;
    }
    
    /* Write register index to IOREGSEL */
    /* I/O APIC registers are 32-bit aligned, so divide by 4 */
    ioapic_base[IOAPIC_REGSEL >> 2] = reg;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
    /* Read register value from IOWIN */
    uint32_t value = ioapic_base[IOAPIC_REGWIN >> 2];
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
    return value;
}

/**
 * @function ioapic_write_register
 * @brief Write I/O APIC register
 * 
 * @param reg Register index
 * @param value Value to write
 */
static void ioapic_write_register(uint8_t reg, uint32_t value)
{
    if (!ioapic_base) {
        return;
    }
    
    /* Write register index to IOREGSEL */
    /* I/O APIC registers are 32-bit aligned, so divide by 4 */
    ioapic_base[IOAPIC_REGSEL >> 2] = reg;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
    
    /* Write register value to IOWIN */
    ioapic_base[IOAPIC_REGWIN >> 2] = value;
    __asm__ volatile ("" ::: "memory"); /* Memory barrier */
}

/**
 * @function ioapic_init
 * @brief Initialize I/O APIC
 * 
 * @return 0 on success, -1 on failure
 */
int ioapic_init(void)
{
    extern void kputs(const char* str);
    extern void kprintf(const char* fmt, ...);
    extern void kprint_hex(uint64_t value);
    extern int paging_map_page_4kb(uint64_t virt, uint64_t phys, uint64_t flags);
    
    #if APIC_DEBUG
    kputs("[IOAPIC-1] Starting I/O APIC initialization\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    
    /* Map I/O APIC registers (4KB, uncached MMIO) */
    /* Use address found from MADT or default */
    uint64_t ioapic_phys = ioapic_base_addr;
    uint64_t ioapic_virt = IOAPIC_MMIO_VIRT;
    uint64_t mmio_flags = PTE_PRESENT | PTE_RW | PTE_PCD; /* PRESENT | RW | PCD (uncached) */
    
    #if APIC_DEBUG
    kprintf("[IOAPIC-1.1] Attempting to map I/O APIC at phys=%llX, virt=%llX\n", 
            (unsigned long long)ioapic_phys, (unsigned long long)ioapic_virt);
    __asm__ volatile ("" ::: "memory");
    #endif
    
    int map_result = paging_map_page_4kb(ioapic_virt, ioapic_phys, mmio_flags);
    if (map_result != 0) {
        kprintf("[IOAPIC-1.2] ERROR: Failed to map I/O APIC page (error=%d)\n", map_result);
        kputs("[IOAPIC-1.3] I/O APIC will not be available, using PIC for external IRQ\n");
        __asm__ volatile ("" ::: "memory");
        return -1;
    }
    
    #if APIC_DEBUG
    kputs("[IOAPIC-1.4] I/O APIC page mapped successfully\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    
    ioapic_base = (volatile uint32_t*)ioapic_virt;
    __asm__ volatile ("" ::: "memory");
    
    #if APIC_DEBUG
    kputs("[IOAPIC-2] Reading I/O APIC ID register\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    
    /* Try to read I/O APIC ID register to verify it's accessible */
    uint32_t id_reg = 0;
    __asm__ volatile ("" ::: "memory");
    id_reg = ioapic_read_register(IOAPIC_ID);
    __asm__ volatile ("" ::: "memory");
    
    #if APIC_DEBUG
    kprintf("[IOAPIC-2.1] I/O APIC ID register value: %x\n", id_reg);
    __asm__ volatile ("" ::: "memory");
    #endif
    
    /* Only 0xFFFFFFFF is a hard failure. ID==0 can be valid (e.g. QEMU). */
    if (id_reg == 0xFFFFFFFF) {
        kputs("[IOAPIC-2.2] WARNING: I/O APIC ID register returns invalid value\n");
        kputs("[IOAPIC-2.3] I/O APIC may not be present at this address; using PIC for external IRQs\n");
        __asm__ volatile ("" ::: "memory");
        return -1;
    }
    
    ioapic_id = (uint8_t)((id_reg >> 24) & 0xFF);
    #if APIC_DEBUG
    kprintf("[IOAPIC-2.4] I/O APIC ID extracted: %x\n", ioapic_id);
    __asm__ volatile ("" ::: "memory");
    #endif
    
    #if APIC_DEBUG
    kputs("[IOAPIC-3] Reading I/O APIC Version register\n");
    __asm__ volatile ("" ::: "memory");
    #endif
    
    uint32_t ver = 0;
    __asm__ volatile ("" ::: "memory");
    ver = ioapic_read_register(IOAPIC_VER);
    __asm__ volatile ("" ::: "memory");
    
    #if APIC_DEBUG
    kprintf("[IOAPIC-3.1] I/O APIC Version register value: %x\n", ver);
    __asm__ volatile ("" ::: "memory");
    #endif

    /* Preserve the current ID/VER state in logs even if the values are invalid. */
    
    /* Check if Version register is readable */
    if (ver == 0xFFFFFFFF) {
        kputs("[IOAPIC-3.2] ERROR: I/O APIC Version register returns 0xFFFFFFFF\n");
        kputs("[IOAPIC-3.3] This usually means I/O APIC is not present or not accessible\n");
        kputs("[IOAPIC-3.4] Possible causes:\n");
        kputs("[IOAPIC-3.5]   1. I/O APIC not enabled in QEMU (use -machine q35)\n");
        kputs("[IOAPIC-3.6]   2. Wrong base address (check ACPI/MADT)\n");
        kputs("[IOAPIC-3.7]   3. Page mapping failed (check [IOAPIC-1.2] above)\n");
        __asm__ volatile ("" ::: "memory");
        return -1;
    }
    
    if (ver == 0x00000000) {
        kputs("[IOAPIC-3.2] ERROR: I/O APIC Version register returns 0x00000000\n");
        kputs("[IOAPIC-3.3] I/O APIC may not be initialized or not present\n");
        __asm__ volatile ("" ::: "memory");
        return -1;
    }
    
    ioapic_version = (uint8_t)(ver & 0xFF);
    /* Bits 16..23 are maximum redirection entry index, so +1 gives count. */
    ioapic_max_redir = (uint8_t)(((ver >> 16) & 0xFF) + 1u);
    
    #if APIC_DEBUG
    kputs("[IOAPIC-4] I/O APIC initialized successfully\n");
    __asm__ volatile ("" ::: "memory");
    kprintf("[IOAPIC-4.1] ID=%x\n", ioapic_id);
    kprintf("[IOAPIC-4.2] Version=%x\n", ioapic_version);
    kprintf("[IOAPIC-4.3] Max Redir Entries=%u\n", ioapic_max_redir);
    __asm__ volatile ("" ::: "memory");
    #endif
    
    /* Validate extracted values */
    if (ioapic_max_redir == 0 || ioapic_max_redir > IOAPIC_MAX_REDIR) {
        #if APIC_DEBUG
        kputs("[IOAPIC-4.4] WARNING: Invalid max redir entries, using default 24\n");
        #endif
        ioapic_max_redir = IOAPIC_MAX_REDIR;
    }

    /* Mask all I/O APIC lines until individual drivers enable required IRQs. */
    uint8_t bsp_lapic_id = apic_get_lapic_id();
    for (uint8_t i = 0; i < ioapic_max_redir; i++) {
        uint32_t rte_low = IOAPIC_RTE_VECTOR(32 + i)
                         | IOAPIC_RTE_DELIVERY_FIXED
                         | IOAPIC_RTE_POLARITY_HIGH
                         | IOAPIC_RTE_TRIGGER_EDGE
                         | IOAPIC_RTE_DEST_MODE_PHYS
                         | IOAPIC_RTE_MASKED;
        uint32_t rte_high = IOAPIC_RTE_DEST_APIC_ID(bsp_lapic_id);
        ioapic_write_register(IOAPIC_REDIR_TBL(i), rte_low);
        ioapic_write_register(IOAPIC_REDIR_TBL_H(i), rte_high);
    }

    /* Apply ACPI ISO routing for legacy IRQ lines (still masked at this stage). */
    for (uint8_t irq = 0; irq < 16; irq++) {
        uint8_t gsi = irq;
        uint32_t polarity = IOAPIC_RTE_POLARITY_HIGH;
        uint32_t trigger = IOAPIC_RTE_TRIGGER_EDGE;
        if (ioapic_route_for_legacy_irq(irq, &gsi, &polarity, &trigger) != 0) {
            continue;
        }
        if (gsi >= ioapic_max_redir) {
            continue;
        }

        uint32_t rte_low = IOAPIC_RTE_VECTOR(32 + irq)
                         | IOAPIC_RTE_DELIVERY_FIXED
                         | polarity
                         | trigger
                         | IOAPIC_RTE_DEST_MODE_PHYS
                         | IOAPIC_RTE_MASKED;
        uint32_t rte_high = IOAPIC_RTE_DEST_APIC_ID(bsp_lapic_id);
        ioapic_write_register(IOAPIC_REDIR_TBL(gsi), rte_low);
        ioapic_write_register(IOAPIC_REDIR_TBL_H(gsi), rte_high);
    }

    #if APIC_DEBUG
    ioapic_log_iso_routes();
    #endif
    
    ioapic_initialized = true;
    ioapic_available = true;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[IOAPIC-OK] I/O APIC is now available for interrupt routing\n");
    __asm__ volatile ("" ::: "memory");
    
    return 0;
}

/**
 * @function apic_get_lapic_id
 * @brief Get Local APIC ID of current CPU
 * 
 * @return LAPIC ID
 * 
 * @note Get current CPU's LAPIC ID
 */
uint32_t apic_get_lapic_id_ext(void)
{
    if (!apic_initialized) {
        return 0;
    }

    uint32_t apic_id_reg = apic_read_register(APIC_ID);

    /* xAPIC keeps the ID in bits 24-31 of the MMIO register; x2APIC exposes
     * the whole 32-bit ID in MSR 0x802 with no shift. Applying the xAPIC
     * shift in x2APIC mode reads back zero for every low ID, which silently
     * collapses all processors onto destination 0. */
    if (lapic_access_mode() == LAPIC_MODE_X2APIC) {
        return apic_id_reg;
    }
    return (apic_id_reg >> 24) & 0xFFu;
}

/* Bring up the calling processor's own LAPIC, in the mode the boot processor
 * already settled on. Much smaller than apic_init(): the I/O APIC, the mode
 * decision and the MMIO mapping are machine-wide and already done. What is
 * genuinely per-CPU is the APIC_BASE enable bit, the spurious-vector register
 * and the local vector table. */
int apic_init_ap(void)
{
    if (lapic_access_init_ap() != 0) {
        return -1;
    }

    apic_reset_local();

    uint32_t svr = apic_read_register(APIC_SVR);
    svr |= APIC_SVR_ENABLE;
    svr &= ~0xFFu;
    svr |= APIC_SVR_SPURIOUS_VECTOR;
    apic_write_register(APIC_SVR, svr);

    /* External IRQs are routed through the I/O APIC to the boot processor;
     * an AP has no business accepting them off its local pins. */
    apic_write_register(APIC_LVT_LINT0, APIC_LVT_MASKED);
    apic_write_register(APIC_LVT_LINT1, APIC_LVT_MASKED);

    return 0;
}

/* Bounded so a wedged ICR fails the send instead of hanging the sender. */
#define APIC_IPI_WAIT_SPINS 100000U

static int apic_send_ipi_raw(uint32_t apic_id, uint32_t icr_low)
{
    if (!apic_initialized || !lapic_access_ready()) {
        return -1;
    }
    if (!lapic_access_ipi_wait(APIC_IPI_WAIT_SPINS)) {
        return -1;
    }
    lapic_access_write_icr(apic_id, icr_low);
    return 0;
}

int apic_send_ipi(uint32_t apic_id, uint8_t vector)
{
    return apic_send_ipi_raw(apic_id,
                             (uint32_t)vector
                             | APIC_ICR_DELIVERY_FIXED
                             | APIC_ICR_DEST_PHYSICAL
                             | APIC_ICR_LEVEL_ASSERT
                             | APIC_ICR_TRIGGER_EDGE
                             | APIC_ICR_SHORTHAND_NONE);
}

int apic_send_ipi_self(uint8_t vector)
{
    /* The self shorthand makes the destination field irrelevant, which also
     * means this works before the sender knows its own APIC ID. */
    return apic_send_ipi_raw(0,
                             (uint32_t)vector
                             | APIC_ICR_DELIVERY_FIXED
                             | APIC_ICR_DEST_PHYSICAL
                             | APIC_ICR_LEVEL_ASSERT
                             | APIC_ICR_TRIGGER_EDGE
                             | APIC_ICR_SHORTHAND_SELF);
}

int apic_send_init(uint32_t apic_id)
{
    return apic_send_ipi_raw(apic_id,
                             APIC_ICR_DELIVERY_INIT
                             | APIC_ICR_DEST_PHYSICAL
                             | APIC_ICR_LEVEL_ASSERT
                             | APIC_ICR_TRIGGER_EDGE
                             | APIC_ICR_SHORTHAND_NONE);
}

int apic_send_startup(uint32_t apic_id, uint8_t start_page)
{
    return apic_send_ipi_raw(apic_id,
                             (uint32_t)start_page
                             | APIC_ICR_DELIVERY_STARTUP
                             | APIC_ICR_DEST_PHYSICAL
                             | APIC_ICR_LEVEL_ASSERT
                             | APIC_ICR_TRIGGER_EDGE
                             | APIC_ICR_SHORTHAND_NONE);
}

/* Send this processor an IPI and confirm it arrives.
 *
 * Runs once per CPU during bring-up. What it proves is narrow but exactly
 * what the AP wake-up depends on: this CPU's ICR accepts a write, the write
 * reaches the local APIC, and the vector comes back through the IDT. A
 * broken ICR encoding -- the easy mistake being the xAPIC register pair
 * versus the single x2APIC MSR -- shows up here rather than as an AP that
 * silently never starts.
 *
 * Interrupts must be masked on entry; a short window is opened deliberately,
 * because an IPI that is only pending proves nothing. */
static volatile uint32_t g_ipi_selftest_hits = 0;

static void apic_ipi_selftest_handler(interrupt_context_t* ctx)
{
    (void)ctx;
    g_ipi_selftest_hits++;
}

int apic_ipi_selftest(void)
{
    if (!apic_initialized) {
        return -1;
    }

    int vector = interrupt_vector_alloc(0xF0, 0xFE);
    if (vector < 0) {
        return -1;
    }

    g_ipi_selftest_hits = 0;
    if (interrupt_register((uint32_t)vector, apic_ipi_selftest_handler) != 0) {
        interrupt_vector_free((uint32_t)vector);
        return -1;
    }

    int rc = -1;
    if (apic_send_ipi_self((uint8_t)vector) == 0) {
        __asm__ volatile ("sti");
        /* Bounded: a vector that never arrives must fail the check, not
         * wedge the boot. */
        for (uint32_t spin = 0; spin < 1000000u; spin++) {
            if (g_ipi_selftest_hits != 0) {
                break;
            }
            __asm__ volatile ("pause");
        }
        __asm__ volatile ("cli");
        rc = (g_ipi_selftest_hits != 0) ? 0 : -1;
    }

    interrupt_unregister((uint32_t)vector);
    interrupt_vector_free((uint32_t)vector);
    return rc;
}

uint8_t apic_get_lapic_id(void)
{
    /* Callers here feed 8-bit destination fields (I/O APIC RTE, MSI address).
     * Truncation is wrong for x2APIC IDs above 255, which needs interrupt
     * remapping to express at all; it is still strictly better than the
     * unconditional shift this used to do. */
    return (uint8_t)(apic_get_lapic_id_ext() & 0xFFu);
}

/**
 * @function apic_enable_irq
 * @brief Enable IRQ in I/O APIC
 * 
 * @param irq IRQ number (0-23)
 * 
 * @note Maps IRQ to interrupt vector (irq + 32)
 * @note Routes to current CPU's LAPIC ID
 * @note Uses edge-triggered, active-high by default (most legacy devices)
 * 
 * @note Route to current CPU, use appropriate trigger/polarity
 */
void apic_enable_irq(uint8_t irq)
{
    if (!ioapic_available || irq >= IOAPIC_MAX_REDIR) {
        return;
    }

    uint8_t gsi = irq;
    uint32_t polarity = IOAPIC_RTE_POLARITY_HIGH;
    uint32_t trigger = IOAPIC_RTE_TRIGGER_EDGE;
    if (ioapic_route_for_legacy_irq(irq, &gsi, &polarity, &trigger) != 0) {
        return;
    }
    if (gsi >= ioapic_max_redir) {
        return;
    }
    
    /* Calculate interrupt vector (IRQ 0 -> vector 32, IRQ 1 -> vector 33, etc.) */
    uint8_t vector = irq + 32;
    
    /* Get current CPU's LAPIC ID */
    uint8_t dest_lapic_id = apic_get_lapic_id();
    
    /* Configure RTE:
     * - Vector: irq + 32
     * - Delivery Mode: Fixed (0)
     * - Destination Mode: Physical (0)
     * - Polarity: Active High (default for most legacy devices)
     * - Trigger: Edge (default for legacy PIC-compatible devices)
     * - Mask: Clear (unmask)
     * - Destination: Current CPU's LAPIC ID
     */
    uint32_t rte_low = vector 
                     | IOAPIC_RTE_DELIVERY_FIXED 
                     | polarity
                     | trigger
                     | IOAPIC_RTE_DEST_MODE_PHYS;
    rte_low &= ~IOAPIC_RTE_MASKED; /* Unmask */
    
    /* Destination: Current CPU's LAPIC ID (bits 24-31) */
    uint32_t rte_high = IOAPIC_RTE_DEST_APIC_ID(dest_lapic_id);
    
    /* Write RTE */
    ioapic_write_register(IOAPIC_REDIR_TBL(gsi), rte_low);
    ioapic_write_register(IOAPIC_REDIR_TBL_H(gsi), rte_high);
}

/**
 * @function apic_disable_irq
 * @brief Disable IRQ in I/O APIC
 * 
 * @param irq IRQ number (0-23)
 */
void apic_disable_irq(uint8_t irq)
{
    if (!ioapic_available || irq >= IOAPIC_MAX_REDIR) {
        return;
    }

    uint8_t gsi = irq;
    if (ioapic_route_for_legacy_irq(irq, &gsi, NULL, NULL) != 0) {
        return;
    }
    if (gsi >= ioapic_max_redir) {
        return;
    }
    
    /* Read current RTE */
    uint32_t rte_low = ioapic_read_register(IOAPIC_REDIR_TBL(gsi));
    
    /* Mask interrupt */
    rte_low |= IOAPIC_RTE_MASKED;
    
    /* Write RTE */
    ioapic_write_register(IOAPIC_REDIR_TBL(gsi), rte_low);
}

/* ============================================================================
 * LAPIC Timer Functions
 * ============================================================================ */

/**
 * @function apic_timer_handler
 * @brief LAPIC timer interrupt handler
 * 
 * @param ctx Interrupt context
 * 
 * @note Minimal work in interrupt handler
 */
void apic_timer_handler(interrupt_context_t* ctx)
{
    (void)ctx;
    /* Every processor runs this handler, so the machine-wide count has to be
     * incremented atomically. Per-CPU counts live in irqstat. */
    __sync_fetch_and_add(&apic_timer_ticks, 1u);

    /*
     * TSC-Deadline mode is one-shot: reprogram the next deadline immediately.
     * We add tsc_per_period to the *current* TSC (not the previous deadline)
     * so any handler latency doesn't accumulate across ticks.
     */
    if (apic_timer_tsc_deadline) {
        wrmsr64(MSR_IA32_TSC_DEADLINE, rdtsc64() + apic_timer_tsc_per_period);
    }
}

/**
 * @function apic_timer_calibrate
 * @brief Calibrate LAPIC timer using PIT as reference
 * 
 * This function calibrates the LAPIC timer by:
 * 1. Setting PIT to a known frequency (100Hz)
 * 2. Starting LAPIC timer with a large count
 * 3. Waiting for PIT interrupt (10ms)
 * 4. Reading LAPIC timer count to calculate frequency
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note Use PIT as reference for calibration
 * @note Avoids division operations - uses bit shifts
 */
static int apic_timer_calibrate(void)
{
    extern int pit_init(uint32_t frequency);
    extern void pit_enable(void);
    extern void pit_disable(void);
    extern uint32_t pit_get_ticks(void);

    /*
     * Calibrate LAPIC timer frequency using PIT as reference.
     *
     * LAPIC timer runs from the CPU bus clock (not the TSC), so its frequency
     * varies per machine and must be measured. We use PIT at 100 Hz (10ms/tick)
     * as a known time source. Measuring over 5 PIT ticks (50ms) reduces the
     * quantisation error from ±10% (single tick) to ±2%.
     */
#define CALIB_PIT_TICKS 5u   /* 5 × 10ms = 50ms calibration window */

    if (pit_init(100) != 0) {
        return -1;
    }

    /* Mask and configure LAPIC timer: one-shot, divide-by-16 */
    uint32_t lvt_timer = apic_read_register(APIC_LVT_TIMER);
    lvt_timer |= APIC_LVT_MASKED;
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
    apic_write_register(APIC_TIMER_DIV, 0b0011);  /* ÷16 */
    lvt_timer &= ~(APIC_LVT_TIMER_PERIODIC | APIC_LVT_MASKED);
    apic_write_register(APIC_LVT_TIMER, lvt_timer);

    /* Start LAPIC countdown from max value */
    apic_write_register(APIC_TIMER_INITCNT, 0xFFFFFFFFu);

    pit_enable();

    /* Temporarily enable interrupts so PIT ticks arrive */
    uint64_t rflags;
    __asm__ volatile ("pushfq; pop %0" : "=r"(rflags));
    bool if_was_set = (rflags & (1ULL << 9)) != 0;
    if (!if_was_set) {
        __asm__ volatile ("sti");
    }

    /* Wait for the first PIT tick edge so we start on a clean boundary */
    uint32_t pit_prev = pit_get_ticks();
    uint32_t spin = 0;
    while (pit_get_ticks() == pit_prev) {
        __asm__ volatile ("pause");
        if (++spin > 20000000U) { break; }
    }

    /* Snapshot LAPIC counter at start of measurement window */
    uint32_t lapic_start = apic_read_register(APIC_TIMER_CURRCNT);
    uint32_t ticks_elapsed = 0;

    /* Count CALIB_PIT_TICKS full PIT ticks */
    while (ticks_elapsed < CALIB_PIT_TICKS) {
        uint32_t cur = pit_get_ticks();
        if (cur != pit_prev) {
            pit_prev = cur;
            ticks_elapsed++;
        }
        __asm__ volatile ("pause");
    }

    uint32_t lapic_end = apic_read_register(APIC_TIMER_CURRCNT);

    if (!if_was_set) {
        __asm__ volatile ("cli");
    }
    pit_disable();

    /* LAPIC counts down, so elapsed = start - end */
    uint32_t lapic_ticks = lapic_start - lapic_end;

    /* lapic_ticks happened in (CALIB_PIT_TICKS * 10) ms */
    uint32_t calib_ms = CALIB_PIT_TICKS * 10u;
    apic_timer_ticks_per_ms = lapic_ticks / calib_ms;

    if (apic_timer_ticks_per_ms == 0 || spin > 20000000U) {
        apic_timer_ticks_per_ms = 10000u; /* safe fallback: ~10 MHz bus */
    }

    kprintf("[APIC-TIMER-CALIB] lapic_ticks=%u over %ums → %u ticks/ms\n",
            lapic_ticks, calib_ms, apic_timer_ticks_per_ms);
    return 0;
#undef CALIB_PIT_TICKS
}

/**
 * @function apic_timer_init
 * @brief Initialize LAPIC timer
 * 
 * @param frequency Desired timer frequency in Hz
 * 
 * @return 0 on success, -1 on failure
 * 
 * @note LAPIC timer is per-CPU and more accurate than PIT
 * @note Timer is calibrated using PIT as reference
 */
int apic_timer_init(uint32_t frequency)
{
    if (!apic_initialized) {
        return -1;
    }

    {
        uint32_t ver = apic_read_register(APIC_VERSION);
        if (ver == 0 || ver == 0xFFFFFFFFu) {
            kprintf("[APIC-TIMER-INIT] LAPIC MMIO unavailable (version=%x)\n", ver);
            return -1;
        }
    }
    
    apic_timer_frequency = frequency;
    scheduler_set_tick_rate(frequency);

    /*
     * Mode selection (XNU-style priority):
     *
     * 1. TSC-Deadline + CPUID 0x15 TSC frequency — zero drift, no calibration
     * 2. TSC-Deadline + PIT-calibrated TSC freq   — zero drift, ~±2% freq error
     * 3. Periodic LAPIC                           — PIT-calibrated, fallback
     */
    extern uint64_t cpu_get_frequency(void);
    uint64_t tsc_hz = cpu_get_frequency();  /* 0 if CPUID 0x15 not available */

    if (apic_tsc_deadline_supported()) {
        if (tsc_hz == 0u) {
            /* TSC-Deadline supported but CPUID 0x15 unavailable — calibrate TSC via PIT */
            if (apic_timer_calibrate() != 0) {
                return -1;
            }
            /* Derive TSC freq: LAPIC runs at bus_freq/16, TSC at cpu_freq.
             * Use LAPIC calibration as a rough estimate; PIT gives ticks_per_ms
             * in LAPIC domain, TSC is typically 3-10× faster. We measure directly. */
            uint64_t tsc0 = rdtsc64();
            /* Reuse the 50ms window already spent in calibrate; just measure TSC
             * against the same LAPIC-calibrated reference. ticks_per_ms *16 / div
             * converts to bus units, but TSC ≠ bus in general. Re-measure. */
            extern uint32_t pit_get_ticks(void);
            extern int pit_init(uint32_t);
            extern void pit_enable(void);
            extern void pit_disable(void);
            pit_init(100);
            pit_enable();
            uint32_t pt = pit_get_ticks();
            uint32_t sp = 0;
            while (pit_get_ticks() == pt) { __asm__ volatile("pause"); if (++sp > 20000000U) break; }
            tsc0 = rdtsc64();
            pt = pit_get_ticks();
            sp = 0;
            while ((pit_get_ticks() - pt) < 5u) { __asm__ volatile("pause"); if (++sp > 50000000U) break; }
            uint64_t tsc1 = rdtsc64();
            pit_disable();
            tsc_hz = (tsc1 - tsc0) * 20u;  /* 5 ticks × 10ms = 50ms → ×20 = Hz */
        }

        apic_timer_tsc_per_period = tsc_hz / (uint64_t)frequency;
        apic_timer_tsc_deadline   = true;
        kprintf("[APIC-TIMER-INIT] mode=TSC-Deadline hz=%u tsc_freq=%llu tsc_per_period=%llu\n",
                frequency,
                (unsigned long long)tsc_hz,
                (unsigned long long)apic_timer_tsc_per_period);
    } else {
        /* Periodic LAPIC fallback */
        if (apic_timer_calibrate() != 0) {
            return -1;
        }

        /* Sanity-check registers before committing to periodic mode */
        apic_write_register(APIC_TIMER_DIV, 0b0011);
        apic_write_register(APIC_LVT_TIMER, APIC_LVT_MASKED | VECTOR_LAPIC_TIMER);
        apic_write_register(APIC_TIMER_INITCNT, 0x1000u);
        if (apic_read_register(APIC_LVT_TIMER) == 0 ||
            apic_read_register(APIC_TIMER_INITCNT) == 0) {
            kprintf("[APIC-TIMER-INIT] LAPIC timer regs unavailable\n");
            return -1;
        }
        kprintf("[APIC-TIMER-INIT] mode=periodic hz=%u ticks_per_ms=%u\n",
                frequency, apic_timer_ticks_per_ms);
    }

    /* Claim the vector only now that a working mode has been established.
     * Registering earlier meant that a failure below left it held by a timer
     * that never started (docs/ru/irq_audit.md, F3). The LAPIC timer has its
     * own vector above the device classes, so it no longer contends with the
     * PIT that calibration borrows -- see vectors.h. */
    extern int interrupt_register(uint32_t vector, interrupt_handler_t handler);
    extern int interrupt_unregister(uint32_t vector);
    if (interrupt_register(VECTOR_LAPIC_TIMER, apic_timer_handler) != 0) {
        return -1;
    }

    /* Calibration helpers (pit_init) may have clobbered the scheduler tick
     * rate.  Restore it to the actual APIC timer frequency before returning. */
    scheduler_set_tick_rate(frequency);

    return 0;
}

/**
 * @function apic_timer_start
 * @brief Start LAPIC timer in periodic mode
 * 
 * @note Uses calibrated frequency for accurate timing
 */
void apic_timer_start(void)
{
    if (!apic_initialized) {
        return;
    }

    if (apic_timer_tsc_deadline) {
        /*
         * TSC-Deadline mode: LVT[18:17]=10, no INITCNT.
         * Write first deadline into MSR — LAPIC fires when RDTSC reaches it.
         * Subsequent deadlines are programmed in the IRQ handler.
         */
        uint32_t lvt = (apic_read_register(APIC_LVT_TIMER) & ~0x000F0000u)
                       | APIC_LVT_TIMER_TSC_DEADLINE
                       | VECTOR_LAPIC_TIMER;
        lvt &= ~APIC_LVT_MASKED;
        apic_write_register(APIC_LVT_TIMER, lvt);
        __asm__ volatile ("" ::: "memory");
        wrmsr64(MSR_IA32_TSC_DEADLINE, rdtsc64() + apic_timer_tsc_per_period);
        kprintf("[APIC-TIMER-START] mode=TSC-Deadline period=%llu tsc\n",
                (unsigned long long)apic_timer_tsc_per_period);
    } else {
        /* Periodic LAPIC mode */
        if (apic_timer_ticks_per_ms == 0u) {
            return;
        }
        uint32_t initial_count = (apic_timer_ticks_per_ms * 1000u) / apic_timer_frequency;
        uint32_t lvt = (apic_read_register(APIC_LVT_TIMER) & ~0x000F0000u)
                       | APIC_LVT_TIMER_PERIODIC
                       | VECTOR_LAPIC_TIMER;
        lvt &= ~APIC_LVT_MASKED;
        apic_write_register(APIC_TIMER_DIV, 0b0011);
        apic_write_register(APIC_LVT_TIMER, lvt);
        apic_write_register(APIC_TIMER_INITCNT, initial_count);
        __asm__ volatile ("" ::: "memory");
        kprintf("[APIC-TIMER-START] mode=periodic init=%u\n", initial_count);
    }
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

    if (apic_timer_tsc_deadline) {
        /* Writing 0 to the deadline MSR disarms the timer */
        wrmsr64(MSR_IA32_TSC_DEADLINE, 0u);
    }
    uint32_t lvt_timer = apic_read_register(APIC_LVT_TIMER);
    lvt_timer |= APIC_LVT_MASKED;
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
}

/**
 * @function apic_timer_get_ticks
 * @brief Get system tick count
 * 
 * @return System tick count (increments at timer frequency)
 * 
 * @note Returns system ticks, not timer register value
 */
uint32_t apic_timer_get_ticks(void)
{
    return apic_timer_ticks;
}

/**
 * @function apic_timer_get_frequency
 * @brief Get timer frequency
 * 
 * @return Timer frequency in Hz
 */
uint32_t apic_timer_get_frequency(void)
{
    return apic_timer_frequency;
}

uint32_t apic_timer_get_lvt_raw(void)
{
    if (!apic_initialized) {
        return 0;
    }
    return apic_read_register(APIC_LVT_TIMER);
}

uint32_t apic_timer_get_initial_count(void)
{
    if (!apic_initialized) {
        return 0;
    }
    return apic_read_register(APIC_TIMER_INITCNT);
}

uint32_t apic_timer_get_current_count(void)
{
    if (!apic_initialized) {
        return 0;
    }
    return apic_read_register(APIC_TIMER_CURRCNT);
}
