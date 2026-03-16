/**
 * @file ktrace.h
 * @brief Kernel event trace ring — compact in-memory record of recent
 *        irq/sched/syscall/fault events, dumped on panic.
 */

#ifndef _RODNIX_COMMON_KTRACE_H
#define _RODNIX_COMMON_KTRACE_H

#include <stdint.h>

/* Event types */
enum {
    KTRACE_SYSCALL = 1,  /* POSIX syscall entry/exit */
    KTRACE_FAULT   = 2,  /* page fault (CR2 as arg) */
    KTRACE_IRQ     = 3,  /* hardware interrupt vector */
    KTRACE_SCHED   = 4   /* scheduler context switch */
};

typedef struct ktrace_event {
    uint32_t type;     /* KTRACE_* */
    uint32_t id;       /* syscall num / irq vector / fault type */
    uint64_t arg;      /* retval (syscall), fault addr (fault), tid (sched) */
    uint64_t ticks;    /* scheduler tick count */
    uint32_t task_id;
    uint32_t thread_id;
} ktrace_event_t;

void ktrace_record(uint32_t type, uint32_t id, uint64_t arg);
void ktrace_syscall(uint32_t num, uint64_t retval);
void ktrace_fault(uint64_t fault_addr);
void ktrace_irq(uint32_t vector);
void ktrace_sched(uint32_t from_tid, uint32_t to_tid);
void ktrace_dump(void);  /* called from panic_dump_state() */

#endif /* _RODNIX_COMMON_KTRACE_H */
