#include "pit.h"
#include "ports.h"
#include "isr.h"
#include "console.h"
#include "irq.h"

static volatile uint64_t ticks = 0;

static void pit_handler(registers_t* r)
{
    (void)r;
    ticks++;
}

uint64_t pit_ticks(void)
{
    return ticks;
}

void pit_init(uint32_t freq_hz)
{
    register_interrupt_handler(32, pit_handler);
    irq_enable(0);

    uint32_t divisor = 1193180 / freq_hz;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

