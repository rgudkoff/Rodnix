/**
 * @file debug.c
 * @brief Debug implementation
 */

#include "../../include/debug.h"
#include "../../include/console.h"
#include "../../include/common.h"
#include "../core/task.h"

#define PANIC_EVENT_MAX 16
#define PANIC_EVENT_LEN 80

static char panic_events[PANIC_EVENT_MAX][PANIC_EVENT_LEN];
static uint32_t panic_event_head = 0;

void debug_event(const char* msg)
{
    if (!msg) {
        return;
    }
    uint32_t idx = panic_event_head++ % PANIC_EVENT_MAX;
    strncpy(panic_events[idx], msg, PANIC_EVENT_LEN - 1);
    panic_events[idx][PANIC_EVENT_LEN - 1] = '\0';
}

static void panic_dump_state(void)
{
    uint64_t rsp = 0;
    uint64_t rbp = 0;
    uint64_t rflags = 0;
    uint64_t cr0 = 0;
    uint64_t cr2 = 0;
    uint64_t cr3 = 0;
    uint64_t cr4 = 0;

    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile ("mov %%rbp, %0" : "=r"(rbp));
    __asm__ volatile ("pushfq; pop %0" : "=r"(rflags));
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    kputs("State:\n");
    kprintf("  RSP=%llx RBP=%llx RFLAGS=%llx\n",
            (unsigned long long)rsp,
            (unsigned long long)rbp,
            (unsigned long long)rflags);
    kprintf("  CR0=%llx CR2=%llx CR3=%llx CR4=%llx\n",
            (unsigned long long)cr0,
            (unsigned long long)cr2,
            (unsigned long long)cr3,
            (unsigned long long)cr4);

    task_t* task = task_get_current();
    thread_t* thread = thread_get_current();
    if (task || thread) {
        kputs("  Current: ");
        if (task) {
            kprintf("task=%llu ", (unsigned long long)task->task_id);
        }
        if (thread) {
            kprintf("thread=%llu ", (unsigned long long)thread->thread_id);
        }
        kputs("\n");
    }

    kputs("Recent events:\n");
    for (uint32_t i = 0; i < PANIC_EVENT_MAX; i++) {
        uint32_t idx = (panic_event_head + i) % PANIC_EVENT_MAX;
        if (panic_events[idx][0] != '\0') {
            kprintf("  - %s\n", panic_events[idx]);
        }
    }
}

__attribute__((noreturn)) void panic(const char* msg)
{
    kputs("\n\n*** KERNEL PANIC ***\n");
    if (msg) {
        kputs("Message: ");
        kputs(msg);
        kputs("\n");
    }
    panic_dump_state();
    kputs("System halted.\n");
    
    __asm__ volatile ("cli; hlt");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

__attribute__((noreturn)) void panicf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    kputs("\n\n*** KERNEL PANIC ***\n");
    kputs("Message: ");
    kvprintf(fmt, args);
    kputs("\n");
    panic_dump_state();
    kputs("System halted.\n");
    
    va_end(args);
    
    __asm__ volatile ("cli; hlt");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
