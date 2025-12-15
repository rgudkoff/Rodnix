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
#include "paging.h"
#include "../../../include/debug.h"
#include "../../core/interrupts.h"
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
 * I/O APIC Register Definitions
 * ============================================================================ */

/* I/O APIC Base Address (default, will be overridden from ACPI MADT if available) */
#define IOAPIC_BASE_ADDR_DEFAULT   0xFEC00000

/* ACPI table signatures */
#define ACPI_SIG_RSDP     "RSD PTR "
#define ACPI_SIG_MADT     "APIC"

/* ACPI MADT structure (XNU-style) */
struct acpi_madt {
    uint32_t signature;      /* "APIC" */
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint32_t lapic_addr;     /* Local APIC address */
    uint32_t flags;
    /* Variable entries follow */
} __attribute__((packed));

/* MADT entry header */
struct madt_entry {
    uint8_t type;
    uint8_t length;
    /* Variable data follows */
} __attribute__((packed));

/* MADT I/O APIC entry (type 1) */
struct madt_ioapic {
    uint8_t  type;           /* 1 */
    uint8_t  length;         /* 12 */
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;    /* I/O APIC base address */
    uint32_t gsi_base;       /* Global System Interrupt base */
} __attribute__((packed));

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

static volatile uint32_t* apic_base = NULL;
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
static uint32_t apic_timer_ticks_per_ms = 0;  /* Calibrated ticks per millisecond */
static uint32_t apic_timer_frequency = 0;     /* Target frequency */
static volatile uint32_t apic_timer_ticks = 0; /* System tick counter */

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/* Forward declarations */
static int ioapic_init(void);

/* XNU-style: Find I/O APIC address from ACPI MADT */
static uint64_t find_ioapic_from_madt(void)
{
    extern void kputs(const char* str);
    extern void kprintf(const char* fmt, ...);
    extern void kprint_hex(uint64_t value);
    
    kputs("[MADT-1] Searching for ACPI MADT table...\n");
    __asm__ volatile ("" ::: "memory");
    
    /* XNU-style: Search for RSDP in BIOS memory area (0xE0000-0xFFFFF) */
    /* RSDP signature: "RSD PTR " (8 bytes) */
    uint64_t rsdp_addr = 0;
    
    /* Search in EBDA and BIOS ROM area */
    for (uint64_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        const char* sig = (const char*)addr;
        if (sig[0] == 'R' && sig[1] == 'S' && sig[2] == 'D' && 
            sig[3] == ' ' && sig[4] == 'P' && sig[5] == 'T' && 
            sig[6] == 'R' && sig[7] == ' ') {
            rsdp_addr = addr;
            kprintf("[MADT-1.1] Found RSDP at 0x%llX\n", (unsigned long long)addr);
            break;
        }
    }
    
    if (rsdp_addr == 0) {
        kputs("[MADT-1.2] RSDP not found in BIOS area, trying Multiboot2...\n");
        /* TODO: Try to get RSDP from Multiboot2 ACPI tag */
        /* For now, return 0 to use default address */
        return 0;
    }
    
    /* Read RSDT/XSDT address from RSDP */
    /* RSDP structure: signature(8), checksum(1), oem_id(6), revision(1), 
     *                  rsdt_addr(4 for v1) or xsdt_addr(8 for v2) */
    uint8_t revision = *((uint8_t*)(rsdp_addr + 15));
    uint64_t table_addr = 0;
    
    if (revision >= 2) {
        /* XSDT (64-bit addresses) */
        table_addr = *((uint64_t*)(rsdp_addr + 24));
        kputs("[MADT-1.3] Using XSDT (ACPI 2.0+)\n");
    } else {
        /* RSDT (32-bit addresses) */
        table_addr = (uint64_t)*((uint32_t*)(rsdp_addr + 16));
        kputs("[MADT-1.4] Using RSDT (ACPI 1.0)\n");
    }
    
    if (table_addr == 0) {
        kputs("[MADT-1.5] RSDT/XSDT address is NULL\n");
        return 0;
    }
    
    kprintf("[MADT-2] RSDT/XSDT at 0x%llX, searching for MADT...\n", 
            (unsigned long long)table_addr);
    __asm__ volatile ("" ::: "memory");
    
    /* Read table header to get entry count */
    uint32_t* header = (uint32_t*)table_addr;
    uint32_t signature = header[0];  /* Should be "RSDT" or "XSDT" */
    uint32_t length = header[1];
    
    /* Calculate number of entries */
    uint32_t entry_count = (length - 36) / (revision >= 2 ? 8 : 4);
    kprintf("[MADT-2.1] Found %u entries in RSDT/XSDT\n", entry_count);
    __asm__ volatile ("" ::: "memory");
    
    /* Search for MADT table */
    uint64_t madt_addr = 0;
    for (uint32_t i = 0; i < entry_count && i < 32; i++) {  /* Limit to 32 entries */
        uint64_t entry_addr;
        if (revision >= 2) {
            entry_addr = ((uint64_t*)table_addr)[9 + i];  /* Skip header (9 qwords) */
        } else {
            entry_addr = (uint64_t)((uint32_t*)table_addr)[9 + i];  /* Skip header (9 dwords) */
        }
        
        /* Check signature */
        uint32_t* entry_sig = (uint32_t*)entry_addr;
        if (entry_sig[0] == 0x43495041) {  /* "APIC" in little-endian */
            madt_addr = entry_addr;
            kprintf("[MADT-2.2] Found MADT at 0x%llX\n", (unsigned long long)entry_addr);
            break;
        }
    }
    
    if (madt_addr == 0) {
        kputs("[MADT-2.3] MADT table not found in RSDT/XSDT\n");
        return 0;
    }
    
    /* Parse MADT to find I/O APIC entry */
    struct acpi_madt* madt = (struct acpi_madt*)madt_addr;
    uint32_t madt_length = madt->length;
    uint8_t* madt_data = (uint8_t*)madt_addr;
    uint32_t offset = sizeof(struct acpi_madt);
    
    kputs("[MADT-3] Parsing MADT entries for I/O APIC...\n");
    __asm__ volatile ("" ::: "memory");
    
    while (offset < madt_length) {
        struct madt_entry* entry = (struct madt_entry*)(madt_data + offset);
        
        if (entry->type == 1) {  /* I/O APIC entry */
            struct madt_ioapic* ioapic_entry = (struct madt_ioapic*)entry;
            uint64_t ioapic_addr = (uint64_t)ioapic_entry->ioapic_addr;
            kprintf("[MADT-3.1] Found I/O APIC entry: ID=0x%02X, addr=0x%llX, GSI_base=%u\n",
                    ioapic_entry->ioapic_id, 
                    (unsigned long long)ioapic_addr,
                    ioapic_entry->gsi_base);
            __asm__ volatile ("" ::: "memory");
            return ioapic_addr;
        }
        
        offset += entry->length;
        if (entry->length == 0) {
            break;  /* Prevent infinite loop */
        }
    }
    
    kputs("[MADT-3.2] I/O APIC entry not found in MADT\n");
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
    
    kputs("[APIC-RDMSR-1] Before RDMSR\n");
    __asm__ volatile ("" ::: "memory");
    
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
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
    __asm__ volatile ("wrmsr" : : "a" (low), "d" (high), "c" (msr));
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
    /* APIC registers are 32-bit aligned, so divide by 4 using bit shift */
    apic_base[offset >> 2] = value;
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
                      : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
                      : "a" (1));
    
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
    
    kputs("[APIC-11] Init I/O APIC\n");
    __asm__ volatile ("" ::: "memory");
    
    /* XNU-style: Find I/O APIC address from ACPI MADT */
    kputs("[APIC-11.0] Searching for I/O APIC in ACPI MADT...\n");
    __asm__ volatile ("" ::: "memory");
    uint64_t ioapic_addr = find_ioapic_from_madt();
    if (ioapic_addr != 0) {
        ioapic_base_addr = ioapic_addr;
        kprintf("[APIC-11.0.1] Found I/O APIC at 0x%llX (from MADT)\n", 
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
        __asm__ volatile ("" ::: "memory");
    } else {
        kputs("[APIC-11.2] I/O APIC init failed (error code above)\n");
        kputs("[APIC-11.3] Will use PIC for external IRQ routing\n");
        kputs("[APIC-11.4] Check [IOAPIC-*] logs above for failure details\n");
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
    
    kputs("[IOAPIC-1] Starting I/O APIC initialization\n");
    __asm__ volatile ("" ::: "memory");
    
    /* Map I/O APIC registers (4KB, uncached MMIO) */
    /* Use address found from MADT or default */
    uint64_t ioapic_phys = ioapic_base_addr;
    uint64_t ioapic_virt = ioapic_phys; /* Identity mapping */
    uint64_t mmio_flags = PTE_PRESENT | PTE_RW | PTE_PCD; /* PRESENT | RW | PCD (uncached) */
    
    kprintf("[IOAPIC-1.1] Attempting to map I/O APIC at phys=0x%llX, virt=0x%llX\n", 
            (unsigned long long)ioapic_phys, (unsigned long long)ioapic_virt);
    __asm__ volatile ("" ::: "memory");
    
    int map_result = paging_map_page_4kb(ioapic_virt, ioapic_phys, mmio_flags);
    if (map_result != 0) {
        kprintf("[IOAPIC-1.2] ERROR: Failed to map I/O APIC page (error=%d)\n", map_result);
        kputs("[IOAPIC-1.3] I/O APIC will not be available, using PIC for external IRQ\n");
        __asm__ volatile ("" ::: "memory");
        return -1;
    }
    
    kputs("[IOAPIC-1.4] I/O APIC page mapped successfully\n");
    __asm__ volatile ("" ::: "memory");
    
    ioapic_base = (volatile uint32_t*)ioapic_virt;
    __asm__ volatile ("" ::: "memory");
    
    kputs("[IOAPIC-2] Reading I/O APIC ID register\n");
    __asm__ volatile ("" ::: "memory");
    
    /* Try to read I/O APIC ID register to verify it's accessible */
    uint32_t id_reg = 0;
    __asm__ volatile ("" ::: "memory");
    id_reg = ioapic_read_register(IOAPIC_ID);
    __asm__ volatile ("" ::: "memory");
    
    kprintf("[IOAPIC-2.1] I/O APIC ID register value: 0x%08X\n", id_reg);
    __asm__ volatile ("" ::: "memory");
    
    /* Check if ID register is readable (should not be all 0xFF or 0x00) */
    if (id_reg == 0xFFFFFFFF || id_reg == 0x00000000) {
        kputs("[IOAPIC-2.2] WARNING: I/O APIC ID register returns invalid value\n");
        kputs("[IOAPIC-2.3] I/O APIC may not be present at this address\n");
        __asm__ volatile ("" ::: "memory");
        /* Continue anyway - some systems may have different behavior */
    }
    
    ioapic_id = (uint8_t)((id_reg >> 24) & 0xFF);
    kprintf("[IOAPIC-2.4] I/O APIC ID extracted: 0x%02X\n", ioapic_id);
    __asm__ volatile ("" ::: "memory");
    
    kputs("[IOAPIC-3] Reading I/O APIC Version register\n");
    __asm__ volatile ("" ::: "memory");
    
    uint32_t ver = 0;
    __asm__ volatile ("" ::: "memory");
    ver = ioapic_read_register(IOAPIC_VER);
    __asm__ volatile ("" ::: "memory");
    
    kprintf("[IOAPIC-3.1] I/O APIC Version register value: 0x%08X\n", ver);
    __asm__ volatile ("" ::: "memory");
    
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
    ioapic_max_redir = (uint8_t)((ver >> 16) & 0xFF);
    
    kputs("[IOAPIC-4] I/O APIC initialized successfully\n");
    __asm__ volatile ("" ::: "memory");
    kprintf("[IOAPIC-4.1] ID=0x%02X\n", ioapic_id);
    kprintf("[IOAPIC-4.2] Version=0x%02X\n", ioapic_version);
    kprintf("[IOAPIC-4.3] Max Redir Entries=%u\n", ioapic_max_redir);
    __asm__ volatile ("" ::: "memory");
    
    /* Validate extracted values */
    if (ioapic_max_redir == 0 || ioapic_max_redir > 24) {
        kputs("[IOAPIC-4.4] WARNING: Invalid max redir entries, using default 24\n");
        ioapic_max_redir = 24;
    }
    
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
 * @note XNU-style: Get current CPU's LAPIC ID
 */
uint8_t apic_get_lapic_id(void)
{
    if (!apic_initialized) {
        return 0;
    }
    
    /* Read APIC ID register */
    uint32_t apic_id_reg = apic_read_register(APIC_ID);
    /* APIC ID is in bits 24-31 */
    return (uint8_t)((apic_id_reg >> 24) & 0xFF);
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
 * @note XNU-style: Route to current CPU, use appropriate trigger/polarity
 */
void apic_enable_irq(uint8_t irq)
{
    if (!ioapic_available || irq >= IOAPIC_MAX_REDIR) {
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
                     | IOAPIC_RTE_POLARITY_HIGH 
                     | IOAPIC_RTE_TRIGGER_EDGE
                     | IOAPIC_RTE_DEST_MODE_PHYS;
    rte_low &= ~IOAPIC_RTE_MASKED; /* Unmask */
    
    /* Destination: Current CPU's LAPIC ID (bits 24-31) */
    uint32_t rte_high = IOAPIC_RTE_DEST_APIC_ID(dest_lapic_id);
    
    /* Write RTE */
    ioapic_write_register(IOAPIC_REDIR_TBL(irq), rte_low);
    ioapic_write_register(IOAPIC_REDIR_TBL_H(irq), rte_high);
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
    
    /* Read current RTE */
    uint32_t rte_low = ioapic_read_register(IOAPIC_REDIR_TBL(irq));
    
    /* Mask interrupt */
    rte_low |= IOAPIC_RTE_MASKED;
    
    /* Write RTE */
    ioapic_write_register(IOAPIC_REDIR_TBL(irq), rte_low);
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
 * @note XNU-style: Minimal work in interrupt handler
 */
void apic_timer_handler(interrupt_context_t* ctx)
{
    (void)ctx;
    /* Increment tick counter */
    apic_timer_ticks++;
    apic_send_eoi();
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
 * @note XNU-style: Use PIT as reference for calibration
 * @note Avoids division operations - uses bit shifts
 */
static int apic_timer_calibrate(void)
{
    extern int pit_init(uint32_t frequency);
    extern void pit_enable(void);
    extern void pit_disable(void);
    extern uint32_t pit_get_ticks(void);
    
    /* Initialize PIT at 100Hz for calibration (10ms per tick) */
    if (pit_init(100) != 0) {
        return -1;
    }
    
    /* Mask LAPIC timer during calibration */
    uint32_t lvt_timer = apic_read_register(APIC_LVT_TIMER);
    lvt_timer |= APIC_LVT_MASKED;
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
    
    /* Set timer divide configuration (divide by 16 for better precision) */
    /* APIC timer divide: 0b0011 = divide by 16 */
    apic_write_register(APIC_TIMER_DIV, 0b0011);
    
    /* Set timer to one-shot mode */
    lvt_timer &= ~APIC_LVT_TIMER_PERIODIC;
    lvt_timer &= ~APIC_LVT_MASKED;
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
    
    /* Start with a large count (will count down) */
    uint32_t start_count = 0xFFFFFFFF;
    apic_write_register(APIC_TIMER_INITCNT, start_count);
    
    /* Wait for 10ms using PIT (100Hz = 10ms per tick) */
    pit_enable();
    uint32_t pit_start = pit_get_ticks();
    while (pit_get_ticks() == pit_start) {
        __asm__ volatile ("pause");
    }
    pit_disable();
    
    /* Read current LAPIC timer count */
    uint32_t end_count = apic_read_register(APIC_TIMER_CURRCNT);
    
    /* Calculate ticks per 10ms */
    uint32_t ticks_10ms = start_count - end_count;
    
    /* Calculate ticks per millisecond */
    /* ticks_10ms / 10 = ticks_per_ms */
    /* Use approximation: (ticks_10ms * 102) >> 10 ≈ ticks_10ms / 10 */
    /* 102/1024 = 0.0996... ≈ 0.1 */
    apic_timer_ticks_per_ms = (ticks_10ms * 102) >> 10;
    
    /* If result is 0, use a safe default */
    if (apic_timer_ticks_per_ms == 0) {
        apic_timer_ticks_per_ms = 10000; /* Default: 10MHz bus frequency */
    }
    
    return 0;
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
    
    apic_timer_frequency = frequency;
    
    /* Calibrate LAPIC timer using PIT */
    if (apic_timer_calibrate() != 0) {
        return -1;
    }
    
    /* Register timer interrupt handler (vector 32) */
    extern int interrupt_register(uint32_t vector, interrupt_handler_t handler);
    
    if (interrupt_register(32, apic_timer_handler) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * @function apic_timer_start
 * @brief Start LAPIC timer in periodic mode
 * 
 * @note XNU-style: Uses calibrated frequency for accurate timing
 */
void apic_timer_start(void)
{
    if (!apic_initialized || apic_timer_ticks_per_ms == 0) {
        return;
    }
    
    /* Calculate initial count for desired frequency */
    /* ticks_per_ms * 1000 / frequency = ticks per period */
    /* For 100Hz: period = 10ms, so initial_count = ticks_per_ms * 10 */
    /* Use bit shift: ticks_per_ms * 10 = (ticks_per_ms << 3) + (ticks_per_ms << 1) */
    uint32_t initial_count;
    if (apic_timer_frequency == 100) {
        /* 100Hz: 10ms per tick = ticks_per_ms * 10 */
        initial_count = (apic_timer_ticks_per_ms << 3) + (apic_timer_ticks_per_ms << 1); /* * 10 */
    } else {
        /* General case: initial_count = (ticks_per_ms * 1000) / frequency */
        /* For other frequencies, we need division, but for now use 100Hz calculation */
        /* TODO: Implement proper division-free calculation for other frequencies */
        initial_count = (apic_timer_ticks_per_ms << 3) + (apic_timer_ticks_per_ms << 1); /* * 10 */
    }
    
    /* Set timer to periodic mode */
    uint32_t lvt_timer = apic_read_register(APIC_LVT_TIMER);
    lvt_timer |= APIC_LVT_TIMER_PERIODIC; /* Periodic mode */
    lvt_timer &= ~APIC_LVT_MASKED; /* Unmask timer */
    lvt_timer |= 32; /* Timer interrupt vector */
    apic_write_register(APIC_LVT_TIMER, lvt_timer);
    
    /* Set initial count to start timer */
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
 * @brief Get system tick count
 * 
 * @return System tick count (increments at timer frequency)
 * 
 * @note XNU-style: Returns system ticks, not timer register value
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

