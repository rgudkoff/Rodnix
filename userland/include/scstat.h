#ifndef _RODNIX_USERLAND_SCSTAT_H
#define _RODNIX_USERLAND_SCSTAT_H

#include <stdint.h>

typedef struct rodnix_scstat_entry {
    uint32_t syscall_no;
    uint32_t reserved0;
    uint64_t int80_count;
    uint64_t fast_count;
    uint64_t total_count;
} rodnix_scstat_entry_t;

#endif /* _RODNIX_USERLAND_SCSTAT_H */
