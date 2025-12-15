/**
 * @file arm64/interrupts.c
 * @brief Interrupt implementation for ARM64
 */

#include "../../core/interrupts.h"
#include "types.h"
#include <stddef.h>

static interrupt_handler_t interrupt_handlers[256];
static irql_t current_irql = IRQL_PASSIVE;

int interrupts_init(void)
{
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = NULL;
    }
    
    current_irql = IRQL_PASSIVE;
    
    /* TODO: Initialize GIC (Generic Interrupt Controller) */
    
    return 0;
}

int interrupt_register(uint32_t vector, interrupt_handler_t handler)
{
    if (vector >= 256 || !handler) {
        return -1;
    }
    
    interrupt_handlers[vector] = handler;
    
    /* TODO: Register in GIC */
    
    return 0;
}

int interrupt_unregister(uint32_t vector)
{
    if (vector >= 256) {
        return -1;
    }
    
    interrupt_handlers[vector] = NULL;
    return 0;
}

void interrupts_enable(void)
{
    __asm__ volatile ("msr daifclr, #2");  /* Clear I bit (IRQ) */
}

void interrupts_disable(void)
{
    __asm__ volatile ("msr daifset, #2");  /* Set I bit (IRQ) */
}

irql_t get_current_irql(void)
{
    return current_irql;
}

irql_t set_irql(irql_t new_level)
{
    irql_t old_level = current_irql;
    current_irql = new_level;
    
    if (new_level > IRQL_PASSIVE) {
        interrupts_disable();
    } else {
        interrupts_enable();
    }
    
    return old_level;
}

void interrupt_wait(void)
{
    __asm__ volatile ("wfi");  /* Wait For Interrupt */
}

int interrupt_send_ipi(uint32_t cpu_id, uint32_t vector)
{
    /* TODO: Implement IPI sending via GIC */
    (void)cpu_id;
    (void)vector;
    return -1;
}

