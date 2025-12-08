#include "debug.h"
#include "console.h"

static void print_reg(const char* name, uint32_t v)
{
    kputs(name);
    kputs("=0x");
    kprint_hex(v);
    kputc(' ');
}

void dump_regs(registers_t* r)
{
    if (!r) return;
    kputs("---- register dump ----\n");
    print_reg("EAX", r->eax); print_reg("EBX", r->ebx); print_reg("ECX", r->ecx); print_reg("EDX", r->edx); kputc('\n');
    print_reg("ESI", r->esi); print_reg("EDI", r->edi); print_reg("EBP", r->ebp); print_reg("ESP", r->esp); kputc('\n');
    print_reg("EIP", r->eip); print_reg("CS ", r->cs ); print_reg("EFL", r->eflags); kputc('\n');
    print_reg("USR", r->useresp); print_reg("SS ", r->ss ); kputc('\n');
    print_reg("INT", r->int_no);  print_reg("ERR", r->err_code); kputc('\n');
    kputs("-----------------------\n");
}

void panic(const char* msg, registers_t* r)
{
    __asm__ volatile("cli");
    kputs("\n!!! KERNEL PANIC !!!\n");
    if (msg) { kputs(msg); kputc('\n'); }
    dump_regs(r);
    kputs("System halted.\n");
    for (;;)
        __asm__ volatile ("hlt");
}

