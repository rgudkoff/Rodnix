/**
 * @file ktrace.c
 * @brief Kernel event trace ring buffer — 128 most recent events, zero-alloc.
 */

#include "ktrace.h"
#include "scheduler.h"
#include "../core/task.h"
#include "../../include/console.h"

#define KTRACE_RING_SIZE 128

static ktrace_event_t ktrace_ring[KTRACE_RING_SIZE];
static uint32_t       ktrace_head = 0;   /* next write position (wraps) */
static uint32_t       ktrace_count = 0;  /* total events ever recorded */

void ktrace_record(uint32_t type, uint32_t id, uint64_t arg)
{
    task_t*   task   = task_get_current();
    thread_t* thread = thread_get_current();

    uint32_t idx = ktrace_head % KTRACE_RING_SIZE;
    ktrace_ring[idx].type      = type;
    ktrace_ring[idx].id        = id;
    ktrace_ring[idx].arg       = arg;
    ktrace_ring[idx].ticks     = scheduler_get_ticks();
    ktrace_ring[idx].task_id   = task   ? (uint32_t)task->task_id   : 0;
    ktrace_ring[idx].thread_id = thread ? (uint32_t)thread->thread_id : 0;
    ktrace_head++;
    ktrace_count++;
}

void ktrace_syscall(uint32_t num, uint64_t retval)
{
    ktrace_record(KTRACE_SYSCALL, num, retval);
}

void ktrace_fault(uint64_t fault_addr)
{
    ktrace_record(KTRACE_FAULT, 0, fault_addr);
}

void ktrace_irq(uint32_t vector)
{
    ktrace_record(KTRACE_IRQ, vector, 0);
}

void ktrace_sched(uint32_t from_tid, uint32_t to_tid)
{
    ktrace_record(KTRACE_SCHED, from_tid, (uint64_t)to_tid);
}

static const char* ktrace_type_name(uint32_t type)
{
    switch (type) {
        case KTRACE_SYSCALL: return "syscall";
        case KTRACE_FAULT:   return "fault  ";
        case KTRACE_IRQ:     return "irq    ";
        case KTRACE_SCHED:   return "sched  ";
        default:             return "?      ";
    }
}

void ktrace_dump(void)
{
    uint32_t total = ktrace_count < KTRACE_RING_SIZE ? ktrace_count : KTRACE_RING_SIZE;
    kprintf("Trace ring (%u events, last %u shown):\n", ktrace_count, total);

    /* Print oldest→newest */
    uint32_t start = ktrace_count > KTRACE_RING_SIZE
                     ? ktrace_head  /* head now points at oldest slot */
                     : 0;
    for (uint32_t i = 0; i < total; i++) {
        uint32_t idx = (start + i) % KTRACE_RING_SIZE;
        const ktrace_event_t* e = &ktrace_ring[idx];
        uint64_t sec = e->ticks / 100u;
        uint64_t ms  = (e->ticks % 100u) * 10u;
        kprintf("  [%4llu.%03llu] %s id=%-4u arg=%llx task=%u\n",
                (unsigned long long)sec,
                (unsigned long long)ms,
                ktrace_type_name(e->type),
                e->id,
                (unsigned long long)e->arg,
                e->task_id);
    }
}
