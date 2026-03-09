/**
 * @file startup_trace.h
 * @brief FreeBSD-style startup/sysinit trace helpers.
 */

#ifndef _RODNIX_COMMON_STARTUP_TRACE_H
#define _RODNIX_COMMON_STARTUP_TRACE_H

#include <stdint.h>

enum startup_sub_id {
    SI_SUB_DUMMY      = 0x0000000,
    SI_SUB_CPU        = 0x2100000,
    SI_SUB_INTR       = 0x2800000,
    SI_SUB_VM         = 0x1000000,
    SI_SUB_CLOCKS     = 0x4800000,
    SI_SUB_SCHED      = 0x2400000,
    SI_SUB_SYSCALLS   = 0xD800000,
    SI_SUB_SECURITY   = 0x2180000,
    SI_SUB_DRIVERS    = 0x3100000,
    SI_SUB_VFS        = 0x4000000,
    SI_SUB_PROTO      = 0x8800000,
    SI_SUB_KTHREAD    = 0xE000000,
    SI_SUB_LAST       = 0xFFFFFFF
};

enum startup_order {
    SI_ORDER_FIRST    = 0x0000000,
    SI_ORDER_SECOND   = 0x0000001,
    SI_ORDER_THIRD    = 0x0000002,
    SI_ORDER_MIDDLE   = 0x1000000,
    SI_ORDER_ANY      = 0xFFFFFFF
};

void startup_trace_init(const char* cmdline);
int startup_trace_bootverbose(void);
int startup_trace_verbose_sysinit(void);
void startup_trace_step_begin(uint32_t subsystem, uint32_t order, const char* name);
void startup_trace_step_end(uint32_t subsystem, uint32_t order, const char* name, int rc);

#endif /* _RODNIX_COMMON_STARTUP_TRACE_H */
