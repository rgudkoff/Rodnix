#ifndef _RODNIX_COMPAT_SYS_MALLOC_H
#define _RODNIX_COMPAT_SYS_MALLOC_H

#include "../../kernel/common/heap.h"

#ifndef M_NOWAIT
#define M_NOWAIT 0x0001
#endif
#ifndef M_WAITOK
#define M_WAITOK 0x0002
#endif
#ifndef M_DEVBUF
#define M_DEVBUF 0
#endif

#define malloc(_sz, _type, _flags) kmalloc((_sz))
#define free(_p, _type) kfree((_p))

#endif
