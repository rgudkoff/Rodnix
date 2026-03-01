/**
 * @file tracev2.h
 * @brief Structured runtime trace events (v2).
 */

#ifndef _RODNIX_COMMON_TRACEV2_H
#define _RODNIX_COMMON_TRACEV2_H

#include <stdint.h>

enum {
    TR2_CAT_BOOT = 1,
    TR2_CAT_SCHED = 2,
    TR2_CAT_MEMORY = 3,
    TR2_CAT_FAULT = 4,
};

enum {
    TR2_EV_SCHED_BLOCK = 1,
    TR2_EV_SCHED_SWITCH = 2,
    TR2_EV_SCHED_REAPER_OVERFLOW = 3,
    TR2_EV_SCHED_EXIT = 4,
};

enum {
    TR2_EV_MEM_INIT_ENTER = 1,
    TR2_EV_MEM_INIT_DONE = 2,
    TR2_EV_MEM_INIT_FAIL = 3,
};

enum {
    TR2_EV_FAULT_EXCEPTION = 1,
    TR2_EV_FAULT_PAGE = 2,
};

void tracev2_emit(uint16_t cat, uint16_t ev, uint64_t a0, uint64_t a1);

#endif /* _RODNIX_COMMON_TRACEV2_H */
