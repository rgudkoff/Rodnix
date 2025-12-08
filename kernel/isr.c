#include "../include/isr.h"
#include "../include/console.h"
#include "../include/pic.h"
#include "../include/ports.h"

static isr_handler_t interrupt_handlers[256];

const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

void isr_handler(struct registers* regs)
{
    if (regs->int_no < 32)
    {
        kputs("\n*** EXCEPTION: ");
        kputs(exception_messages[regs->int_no]);
        kputs(" ***\n");
        kputs("System halted.\n");
        for (;;) { }
    }
    
    if (interrupt_handlers[regs->int_no] != 0)
        interrupt_handlers[regs->int_no](regs);
}

void irq_handler(struct registers* regs)
{
    uint8_t irq_num = regs->int_no;
    
    /* Отправить EOI в PIC */
    if (irq_num >= 40)
        outb(0xA0, 0x20);  /* Slave PIC */
    outb(0x20, 0x20);      /* Master PIC */
    
    if (irq_num < 256 && interrupt_handlers[irq_num] != 0)
        interrupt_handlers[irq_num](regs);
}

void isr_init(void)
{
    for (int i = 0; i < 256; i++)
        interrupt_handlers[i] = 0;
}

void register_interrupt_handler(uint8_t n, isr_handler_t handler)
{
    interrupt_handlers[n] = handler;
}

