/**
 * @file interrupts_stub.c
 * @brief Stub for interrupt registration
 */

#include <stdint.h>

/* Temporary stub for register_interrupt_handler */
void register_interrupt_handler(uint32_t vector, void (*handler)(void*))
{
    /* TODO: Implement actual interrupt handler registration */
    (void)vector;
    (void)handler;
}

