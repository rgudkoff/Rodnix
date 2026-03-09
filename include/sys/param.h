#ifndef _RODNIX_COMPAT_SYS_PARAM_H
#define _RODNIX_COMPAT_SYS_PARAM_H

#include <sys/types.h>

#define PAGE_SIZE 4096
#define PAGE_MASK (PAGE_SIZE - 1)
#define PAGE_SHIFT 12

#ifndef MIN
#define MIN(_a,_b) ((_a) < (_b) ? (_a) : (_b))
#endif
#ifndef MAX
#define MAX(_a,_b) ((_a) > (_b) ? (_a) : (_b))
#endif

#endif
