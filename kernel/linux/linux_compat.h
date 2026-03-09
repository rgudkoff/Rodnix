#ifndef _RODNIX_LINUX_COMPAT_H
#define _RODNIX_LINUX_COMPAT_H

#include <stdint.h>

int linux_compat_init(void);
uint64_t linux_compat_dispatch(uint64_t num,
                               uint64_t a1,
                               uint64_t a2,
                               uint64_t a3,
                               uint64_t a4,
                               uint64_t a5,
                               uint64_t a6);
void linux_compat_apply_user_state(void);
void linux_compat_trace_dump_recent(void);

#endif /* _RODNIX_LINUX_COMPAT_H */
