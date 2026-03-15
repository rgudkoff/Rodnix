/**
 * @file debug.c
 * @brief Debug implementation
 */

#include "../../include/debug.h"
#include "../../include/console.h"
#include "../../include/common.h"
#include "../core/task.h"
#include "ktrace.h"

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

/* Walk frame pointer chain and print return addresses. Stops at NULL/invalid
 * rbp or after PANIC_BT_MAX frames to avoid looping on corrupt stacks. */
#define PANIC_BT_MAX 16
#define KERNEL_VIRT_BASE 0xffffffff80000000ULL

static void panic_backtrace(void)
{
    uint64_t rbp = 0;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(rbp));

    kputs("Backtrace:\n");
    for (int i = 0; i < PANIC_BT_MAX && rbp != 0; i++) {
        /* Sanity: rbp must be in kernel virtual space and 8-byte aligned. */
        if (rbp < KERNEL_VIRT_BASE || (rbp & 7) != 0) {
            break;
        }
        uint64_t* frame  = (uint64_t*)rbp;
        uint64_t ret_ip  = frame[1];
        uint64_t next_bp = frame[0];
        kprintf("  #%-2d  0x%016llx\n", i, (unsigned long long)ret_ip);
        if (next_bp == 0 || next_bp <= rbp) {
            break;
        }
        rbp = next_bp;
    }
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

    kputs("Registers:\n");
    kprintf("  RSP=%016llx  RBP=%016llx  RFLAGS=%016llx\n",
            (unsigned long long)rsp,
            (unsigned long long)rbp,
            (unsigned long long)rflags);
    kprintf("  CR0=%016llx  CR2=%016llx  CR3=%016llx  CR4=%016llx\n",
            (unsigned long long)cr0,
            (unsigned long long)cr2,
            (unsigned long long)cr3,
            (unsigned long long)cr4);

    task_t* task = task_get_current();
    thread_t* thread = thread_get_current();
    kputs("Context:\n");
    if (task) {
        kprintf("  task=%llu  uid=%u  euid=%u  state=%d\n",
                (unsigned long long)task->task_id,
                task->uid, task->euid, (int)task->state);
    } else {
        kputs("  task=<none>\n");
    }
    if (thread) {
        kprintf("  thread=%llu\n", (unsigned long long)thread->thread_id);
    }

    panic_backtrace();
    ktrace_dump();

    kputs("Recent debug events:\n");
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
