#include "../include/isr.h"
#include "../include/console.h"
#include "../include/common.h"
#include "../include/paging.h"
#include "../include/pmm.h"

/* Обработчик page fault для отладки */
static void page_fault_handler(struct registers* regs)
{
    uint32_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    
    kputs("\n*** PAGE FAULT ***\n");
    kputs("Fault address: 0x");
    kprint_hex(fault_address);
    kputs("\n");
    
    kputs("Error code: 0x");
    kprint_hex(regs->err_code);
    kputs("\n");
    
    kputs("EIP: 0x");
    kprint_hex(regs->eip);
    kputs("\n");
    
    kputs("CS: 0x");
    kprint_hex(regs->cs);
    kputs("\n");
    
    /* Расшифровка error code */
    uint32_t present = regs->err_code & 0x1;
    uint32_t write = regs->err_code & 0x2;
    uint32_t user = regs->err_code & 0x4;
    uint32_t reserved = regs->err_code & 0x8;
    uint32_t instruction = regs->err_code & 0x10;
    
    kputs("Details: ");
    if (!present) kputs("not present ");
    if (write) kputs("write ");
    if (user) kputs("user ");
    if (reserved) kputs("reserved ");
    if (instruction) kputs("instruction fetch ");
    kputs("\n");
    
    /* Проверяем, отображена ли страница */
    uint32_t phys = paging_get_physical(fault_address);
    kputs("Physical address: 0x");
    kprint_hex(phys);
    kputs("\n");
    
    kputs("System halted.\n");
    for (;;) { }
}

/* Регистрация обработчика page fault */
void paging_debug_init(void)
{
    register_interrupt_handler(14, page_fault_handler);
}

