/**
 * @file pic.c
 * @brief PIC (Programmable Interrupt Controller) implementation for x86_64
 * 
 * This module implements the 8259A PIC initialization and management.
 * The PIC is used to route hardware interrupts to the CPU before APIC is available.
 * 
 * @note This implementation follows XNU-style architecture but is adapted for RodNIX.
 * @note On modern systems, APIC is preferred, but PIC initialization is still required
 *       for compatibility and as a fallback.
 */

#include <stdint.h>
#include <stdbool.h>
#include "../../include/console.h"

/* ============================================================================
 * PIC I/O Ports
 * ============================================================================ */

/* Master PIC (IRQ 0-7) */
#define PIC1_COMMAND    0x20    /* Master PIC command port */
#define PIC1_DATA       0x21    /* Master PIC data port */

/* Slave PIC (IRQ 8-15) */
#define PIC2_COMMAND    0xA0    /* Slave PIC command port */
#define PIC2_DATA       0xA1     /* Slave PIC data port */

/* ============================================================================
 * PIC Command Constants
 * ============================================================================ */

#define PIC_EOI         0x20    /* End of Interrupt command */
#define PIC_ICW1_INIT   0x11    /* ICW1: Initialization command (edge triggered, cascade) */
#define PIC_ICW1_ICW4   0x01    /* ICW1: ICW4 needed flag */

/* ============================================================================
 * PIC Initialization Constants
 * ============================================================================ */

#define PIC_IRQ_BASE_MASTER  0x20    /* Base interrupt vector for master PIC (IRQ 0-7 -> 0x20-0x27) */
#define PIC_IRQ_BASE_SLAVE   0x28    /* Base interrupt vector for slave PIC (IRQ 8-15 -> 0x28-0x2F) */
#define PIC_CASCADE_IRQ      0x02    /* IRQ line used for master-slave cascade */
#define PIC_ICW4_8086_MODE   0x01    /* ICW4: 8086/8088 mode (not 8080/8085) */

/* ============================================================================
 * Public Interface
 * ============================================================================ */

/**
 * @function pic_init
 * @brief Initialize the 8259A Programmable Interrupt Controller
 * 
 * This function performs the PIC initialization sequence:
 * 1. Saves current interrupt masks
 * 2. Sends ICW1 (Initialization Command Word 1) to both PICs
 * 3. Sends ICW2 (sets interrupt vector offsets)
 * 4. Sends ICW3 (configures master-slave cascade)
 * 5. Sends ICW4 (sets 8086 mode and other options)
 * 6. Restores interrupt masks
 * 
 * After initialization:
 * - Master PIC IRQs (0-7) map to interrupt vectors 0x20-0x27
 * - Slave PIC IRQs (8-15) map to interrupt vectors 0x28-0x2F
 * - Slave PIC is cascaded to master PIC on IRQ 2
 * 
 * @note This must be called before enabling interrupts.
 * @note The PIC is initialized in 8086 mode (not 8080/8085 mode).
 */
void pic_init(void)
{
    /* Save current interrupt masks (to restore later) */
    uint8_t a1, a2;
    __asm__ volatile ("inb %1, %0" : "=a"(a1) : "Nd"(PIC1_DATA));
    __asm__ volatile ("inb %1, %0" : "=a"(a2) : "Nd"(PIC2_DATA));
    __asm__ volatile ("" ::: "memory");
    
    /* ICW1: Start initialization sequence
     * Bit 4 = 1: ICW1 command
     * Bit 0 = 1: ICW4 needed
     * Other bits: Edge triggered, cascade mode
     */
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_ICW1_INIT), "Nd"(PIC1_COMMAND));
    __asm__ volatile ("" ::: "memory");
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_ICW1_INIT), "Nd"(PIC2_COMMAND));
    __asm__ volatile ("" ::: "memory");
    
    /* ICW2: Set interrupt vector offsets
     * Master PIC: IRQ 0-7 -> interrupt vectors 0x20-0x27
     * Slave PIC: IRQ 8-15 -> interrupt vectors 0x28-0x2F
     */
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_IRQ_BASE_MASTER), "Nd"(PIC1_DATA));
    __asm__ volatile ("" ::: "memory");
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_IRQ_BASE_SLAVE), "Nd"(PIC2_DATA));
    __asm__ volatile ("" ::: "memory");
    
    /* ICW3: Master/slave configuration
     * Master PIC: Bit 2 = 1 (slave connected to IRQ 2)
     * Slave PIC: Value = 2 (cascade identity - connected to master's IRQ 2)
     */
    __asm__ volatile ("outb %%al, %1" : : "a"(1 << PIC_CASCADE_IRQ), "Nd"(PIC1_DATA));
    __asm__ volatile ("" ::: "memory");
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_CASCADE_IRQ), "Nd"(PIC2_DATA));
    __asm__ volatile ("" ::: "memory");
    
    /* ICW4: Additional configuration
     * Bit 0 = 1: 8086/8088 mode (not 8080/8085)
     * Other bits: Normal end of interrupt, not buffered, not special fully nested
     */
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_ICW4_8086_MODE), "Nd"(PIC1_DATA));
    __asm__ volatile ("" ::: "memory");
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_ICW4_8086_MODE), "Nd"(PIC2_DATA));
    __asm__ volatile ("" ::: "memory");
    
    /* Restore saved interrupt masks */
    __asm__ volatile ("outb %%al, %1" : : "a"(a1), "Nd"(PIC1_DATA));
    __asm__ volatile ("" ::: "memory");
    __asm__ volatile ("outb %%al, %1" : : "a"(a2), "Nd"(PIC2_DATA));
    __asm__ volatile ("" ::: "memory");
}

/**
 * @function pic_send_eoi
 * @brief Send End of Interrupt (EOI) command to PIC
 * 
 * This function must be called at the end of an interrupt handler to
 * acknowledge the interrupt and allow the PIC to send further interrupts.
 * 
 * @param irq IRQ number (0-15)
 * 
 * @note For IRQ >= 8, EOI must be sent to both slave and master PICs.
 * @note For IRQ < 8, EOI is sent only to master PIC.
 * 
 * @warning Failure to send EOI will prevent further interrupts from that IRQ.
 */
void pic_send_eoi(uint8_t irq)
{
    /* If IRQ is from slave PIC (IRQ >= 8), send EOI to slave first */
    if (irq >= 8) {
        __asm__ volatile ("outb %%al, %1" : : "a"(PIC_EOI), "Nd"(PIC2_COMMAND));
    }
    /* Always send EOI to master PIC (required for cascade) */
    __asm__ volatile ("outb %%al, %1" : : "a"(PIC_EOI), "Nd"(PIC1_COMMAND));
}

/* Disable PIC (mask all interrupts) */
void pic_disable(void)
{
    __asm__ volatile ("outb %%al, %1" : : "a"(0xFF), "Nd"(PIC1_DATA));
    __asm__ volatile ("outb %%al, %1" : : "a"(0xFF), "Nd"(PIC2_DATA));
}

/* Enable specific IRQ (XNU-style: use direct port constants) */
void pic_enable_irq(uint8_t irq)
{
    /* DIAGNOSTIC: Mark on VGA (RED) */
    static volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    static uint32_t enable_count = 0;
    if (enable_count < 10) {
        vga_debug[80 * 17 + enable_count] = 0x0C00 | ('P');  /* RED - PIC enable */
        vga_debug[80 * 17 + enable_count + 1] = 0x0C00 | ('0' + irq);  /* RED */
        enable_count += 2;
    }
    
    /* NOTE: Do NOT use kprintf here - it may be called from interrupt context! */
    /* Use only VGA output for diagnostics */
    
    uint8_t value;
    uint8_t bit = irq;
    
    /* XNU-style: Use direct port constants instead of variables */
    /* In x86_64, outb requires port to be constant or in dx register */
    if (irq < 8) {
        /* Master PIC - use constant port directly */
        if (enable_count < 10) {
            vga_debug[80 * 17 + enable_count] = 0x0C00 | ('M');  /* RED - Master */
            enable_count++;
        }
        __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(PIC1_DATA));
        value &= ~(1 << bit);
        /* Use simple outb with constant port (same as pic_init) */
        __asm__ volatile ("outb %%al, %1" : : "a"(value), "Nd"(PIC1_DATA));
        if (enable_count < 10) {
            vga_debug[80 * 17 + enable_count] = 0x0C00 | ('D');  /* RED - Done */
            enable_count++;
        }
    } else {
        /* Slave PIC - use constant port directly */
        bit = irq - 8;
        __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(PIC2_DATA));
        value &= ~(1 << bit);
        __asm__ volatile ("outb %%al, %1" : : "a"(value), "Nd"(PIC2_DATA));
    }
    
    if (enable_count < 10) {
        vga_debug[80 * 17 + enable_count] = 0x0C00 | ('E');  /* RED - Enabled */
        enable_count++;
    }
}

/* Disable specific IRQ */
void pic_disable_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    value |= (1 << irq);
    __asm__ volatile ("outb %%al, %1" : : "a"(value), "Nd"(port));
}

