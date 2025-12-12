/**
 * @file idt.c
 * @brief Interrupt Descriptor Table (IDT) implementation for x86_64
 * 
 * This module implements the IDT for x86_64 architecture. The IDT maps
 * interrupt vectors (0-255) to their respective interrupt handlers.
 * 
 * @note This implementation follows XNU-style architecture but is adapted for RodNIX.
 */

#include "types.h"
#include "config.h"
#include "../../include/debug.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * IDT Entry Structure (x86_64)
 * ============================================================================ */

/**
 * @struct idt_entry
 * @brief IDT entry structure for x86_64
 * 
 * In 64-bit mode, IDT entries are 16 bytes:
 * - Offset: 64-bit address (split into low, mid, high)
 * - Selector: 16-bit code segment selector
 * - IST: 8-bit Interrupt Stack Table index
 * - Type/Attributes: 8-bit flags
 * - Reserved: 32 bits
 */
struct idt_entry {
    uint16_t offset_low;      /* Lower 16 bits of handler address */
    uint16_t selector;        /* Code segment selector */
    uint8_t ist;              /* Interrupt Stack Table index */
    uint8_t type_attr;        /* Type and attributes */
    uint16_t offset_mid;      /* Middle 16 bits of handler address */
    uint32_t offset_high;     /* Upper 32 bits of handler address */
    uint32_t reserved;        /* Reserved (must be 0) */
} __attribute__((packed));

/**
 * @struct idt_ptr
 * @brief IDT pointer structure for LIDT instruction
 */
struct idt_ptr {
    uint16_t limit;           /* Size of IDT minus 1 */
    uint64_t base;            /* Base address of IDT */
} __attribute__((packed));

/* ============================================================================
 * IDT Table Storage
 * ============================================================================ */

/* IDT table: 256 entries for all possible interrupt vectors */
static struct idt_entry idt[256] __attribute__((aligned(16)));

/* IDT descriptor pointer for LIDT instruction */
static struct idt_ptr idt_pointer;

/* External ISR handlers (defined in assembly) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * @function idt_set_entry
 * @brief Set an IDT entry with the specified parameters (internal helper)
 * 
 * @param num Interrupt vector number (0-255)
 * @param base 64-bit address of interrupt handler
 * @param selector Code segment selector (typically 0x08 for kernel code)
 * @param type_attr Type and attributes byte
 * @param ist Interrupt Stack Table index (0 = use regular stack)
 * 
 * @note This function does not validate parameters. Caller must ensure
 *       num < 256 and base is a valid handler address.
 */
static void idt_set_entry(uint8_t num, uint64_t base, uint16_t selector, uint8_t type_attr, uint8_t ist)
{
    idt[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].selector = selector;
    idt[num].ist = ist;
    idt[num].type_attr = type_attr;
    idt[num].reserved = 0;
}

/* ============================================================================
 * IDT Entry Type Attributes
 * ============================================================================ */

/* IDT entry type and attributes flags */
#define IDT_TYPE_INTERRUPT_GATE  0x8E  /* 64-bit interrupt gate (IF cleared on entry) */
#define IDT_TYPE_TRAP_GATE       0x8F  /* 64-bit trap gate (IF not cleared) */
#define IDT_TYPE_TASK_GATE       0x85  /* Task gate (not used in 64-bit mode) */

/* Bit fields for type_attr byte:
 *   Bit 7: Present (1 = present, 0 = not present)
 *   Bits 5-6: DPL (Descriptor Privilege Level: 0 = kernel, 3 = user)
 *   Bit 4: Storage segment (0 for interrupt/trap gates)
 *   Bits 0-3: Gate type (0xE = interrupt, 0xF = trap)
 */

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function idt_init
 * @brief Initialize the Interrupt Descriptor Table
 * 
 * This function:
 * 1. Sets up the IDT pointer structure
 * 2. Clears all IDT entries
 * 3. Registers exception handlers (vectors 0-31)
 * 4. Registers IRQ handlers (vectors 32-47)
 * 5. Loads the IDT using LIDT instruction
 * 
 * @return 0 on success
 * 
 * @note Exception handlers are set up as interrupt gates with DPL=0 (kernel only).
 * @note IRQ handlers are mapped to vectors 32-47 (after PIC remapping).
 */
int idt_init(void)
{
    extern void kputs(const char* str);
    
    /* Step 1: Setup IDT pointer */
    kputs("[IDT-1] Setup pointer\n");
    __asm__ volatile ("" ::: "memory");
    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (uint64_t)&idt;
    __asm__ volatile ("" ::: "memory");
    
    /* Step 2: Clear IDT entries */
    kputs("[IDT-2] Clear entries\n");
    __asm__ volatile ("" ::: "memory");
    for (int i = 0; i < 256; i++) {
        idt_set_entry(i, 0, 0, 0, 0);
    }
    __asm__ volatile ("" ::: "memory");
    
    /* Step 3: Setup exception handlers (vectors 0-31)
     * XNU-style: call idt_set_entry directly for each handler
     */
    kputs("[IDT-3] Setup ISR 0-31\n");
    __asm__ volatile ("" ::: "memory");
    idt_set_entry(0, (uint64_t)isr0, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(1, (uint64_t)isr1, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(2, (uint64_t)isr2, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(3, (uint64_t)isr3, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(4, (uint64_t)isr4, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(5, (uint64_t)isr5, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(6, (uint64_t)isr6, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(7, (uint64_t)isr7, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(8, (uint64_t)isr8, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(9, (uint64_t)isr9, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(10, (uint64_t)isr10, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(11, (uint64_t)isr11, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(12, (uint64_t)isr12, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(13, (uint64_t)isr13, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(14, (uint64_t)isr14, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(15, (uint64_t)isr15, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(16, (uint64_t)isr16, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(17, (uint64_t)isr17, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(18, (uint64_t)isr18, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(19, (uint64_t)isr19, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(20, (uint64_t)isr20, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(21, (uint64_t)isr21, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(22, (uint64_t)isr22, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(23, (uint64_t)isr23, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(24, (uint64_t)isr24, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(25, (uint64_t)isr25, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(26, (uint64_t)isr26, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(27, (uint64_t)isr27, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(28, (uint64_t)isr28, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(29, (uint64_t)isr29, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(30, (uint64_t)isr30, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(31, (uint64_t)isr31, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    __asm__ volatile ("" ::: "memory");
    
    /* Step 4: Setup IRQ handlers (vectors 32-47) */
    kputs("[IDT-4] Setup IRQ 32-47\n");
    __asm__ volatile ("" ::: "memory");
    idt_set_entry(32, (uint64_t)irq0, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(33, (uint64_t)irq1, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(34, (uint64_t)irq2, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(35, (uint64_t)irq3, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(36, (uint64_t)irq4, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(37, (uint64_t)irq5, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(38, (uint64_t)irq6, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(39, (uint64_t)irq7, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(40, (uint64_t)irq8, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(41, (uint64_t)irq9, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(42, (uint64_t)irq10, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(43, (uint64_t)irq11, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(44, (uint64_t)irq12, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(45, (uint64_t)irq13, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(46, (uint64_t)irq14, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    idt_set_entry(47, (uint64_t)irq15, 0x08, IDT_TYPE_INTERRUPT_GATE, 0);
    __asm__ volatile ("" ::: "memory");
    
    /* Step 5: Load IDT */
    kputs("[IDT-5] Load IDT\n");
    __asm__ volatile ("" ::: "memory");
    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));
    __asm__ volatile ("" ::: "memory");
    
    kputs("[IDT-OK] Complete\n");
    return 0;
}

/**
 * @function idt_get_handler
 * @brief Get the handler address for a given interrupt vector
 * 
 * @param vector Interrupt vector number (0-255)
 * @return Handler address, or NULL if vector is invalid
 */
void* idt_get_handler(uint8_t vector)
{
    if (vector >= 256) {
        return NULL;
    }
    
    uint64_t offset = (uint64_t)idt[vector].offset_low |
                     ((uint64_t)idt[vector].offset_mid << 16) |
                     ((uint64_t)idt[vector].offset_high << 32);
    
    return (void*)offset;
}

/**
 * @function idt_set_handler
 * @brief Set a custom handler for an interrupt vector
 * 
 * @param vector Interrupt vector number (0-255)
 * @param handler Handler function address
 * @param type_attr Type and attributes byte
 * @param ist Interrupt Stack Table index
 * 
 * @return 0 on success, -1 on failure
 */
int idt_set_handler(uint8_t vector, void* handler, uint8_t type_attr, uint8_t ist)
{
    if (vector >= 256 || !handler) {
        return -1;
    }
    
    idt_set_entry(vector, (uint64_t)handler, 0x08, type_attr, ist);
    return 0;
}
