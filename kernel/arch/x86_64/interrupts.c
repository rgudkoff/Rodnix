/**
 * @file x86_64/interrupts.c
 * @brief Реализация прерываний для x86_64
 */

#include "../../core/interrupts.h"
#include "types.h"
#include <stddef.h>
#include <stdbool.h>

/* Временная структура для совместимости со старым кодом */
struct registers {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rsp_orig;
    uint64_t rbx, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
};

/* Глобальный массив обработчиков */
static interrupt_handler_t interrupt_handlers[256];
static irql_t current_irql = IRQL_PASSIVE;

/* Временная функция для регистрации обработчиков (будет заменена) */
extern void register_interrupt_handler(uint32_t vector, void (*handler)(struct registers*));

/* Преобразование архитектурно-зависимого контекста */
static void convert_interrupt_context(struct registers* regs, interrupt_context_t* ctx)
{
    ctx->pc = regs->rip;
    ctx->sp = regs->rsp;
    ctx->flags = regs->rflags;
    ctx->error_code = regs->err_code;
    ctx->vector = regs->int_no;
    ctx->type = (regs->int_no < 32) ? INTERRUPT_TYPE_EXCEPTION : INTERRUPT_TYPE_IRQ;
    
    /* Сохраняем архитектурно-зависимые данные */
    static x86_64_interrupt_context_t arch_ctx_storage[256];
    x86_64_interrupt_context_t* arch_ctx = &arch_ctx_storage[regs->int_no];
    
    arch_ctx->regs.rip = regs->rip;
    arch_ctx->regs.rsp = regs->rsp;
    arch_ctx->regs.rflags = regs->rflags;
    arch_ctx->error_code = regs->err_code;
    arch_ctx->vector = regs->int_no;
    
    ctx->arch_specific = arch_ctx;
}

/* Обертка для обработчиков прерываний x86_64 */
static void interrupt_wrapper(struct registers* regs)
{
    interrupt_context_t ctx;
    convert_interrupt_context(regs, &ctx);
    
    /* Вызываем зарегистрированный обработчик */
    if (interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](&ctx);
    }
}

int interrupts_init(void)
{
    /* Очистка массива обработчиков */
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = NULL;
    }
    
    current_irql = IRQL_PASSIVE;
    
    /* TODO: Инициализация IDT, PIC, APIC */
    
    return 0;
}

int interrupt_register(uint32_t vector, interrupt_handler_t handler)
{
    if (vector >= 256 || !handler) {
        return -1;
    }
    
    interrupt_handlers[vector] = handler;
    
    /* Регистрация в системе прерываний x86_64 */
    register_interrupt_handler(vector, interrupt_wrapper);
    
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
    __asm__ volatile ("sti");
}

void interrupts_disable(void)
{
    __asm__ volatile ("cli");
}

irql_t get_current_irql(void)
{
    return current_irql;
}

irql_t set_irql(irql_t new_level)
{
    irql_t old_level = current_irql;
    current_irql = new_level;
    
    /* Если переходим на более высокий уровень, отключаем прерывания */
    if (new_level > IRQL_PASSIVE) {
        interrupts_disable();
    } else {
        interrupts_enable();
    }
    
    return old_level;
}

void interrupt_wait(void)
{
    __asm__ volatile ("hlt");
}

int interrupt_send_ipi(uint32_t cpu_id, uint32_t vector)
{
    /* TODO: Реализовать отправку IPI через APIC */
    (void)cpu_id;
    (void)vector;
    return -1;
}

