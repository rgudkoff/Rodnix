#include "console.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "pit.h"
#include "keyboard.h"

static void banner(void)
{
    kputs("RodNIX booting...\n");
}

void kmain(void)
{
    console_init();
    banner();

    gdt_init();
    idt_init();
    irq_init();
    pit_init(100);
    keyboard_init();

    kputs("Init done. Interrupts on.\n");
    __asm__ volatile ("sti");

    for (;;)
        __asm__ volatile ("hlt");
}

