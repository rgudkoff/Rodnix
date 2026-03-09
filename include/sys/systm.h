#ifndef _RODNIX_COMPAT_SYS_SYSTM_H
#define _RODNIX_COMPAT_SYS_SYSTM_H

#include <sys/types.h>
#include "../../include/common.h"
#include "../../include/console.h"

#define bzero(_p,_n) memset((_p), 0, (_n))
#define bcopy(_s,_d,_n) memmove((_d), (_s), (_n))

#define printf kprintf
#define device_printf(_dev, _fmt, ...) kprintf(_fmt, ##__VA_ARGS__)

#define panic(_fmt, ...) kprintf("panic: " _fmt "\n", ##__VA_ARGS__)
#define DELAY(_us) do { (void)(_us); } while (0)
#define pause(_wchan, _ticks) do { (void)(_wchan); (void)(_ticks); } while (0)

#ifndef min
#define min(_a,_b) ((_a) < (_b) ? (_a) : (_b))
#endif
#ifndef max
#define max(_a,_b) ((_a) > (_b) ? (_a) : (_b))
#endif

#define KASSERT(_cond, _msg) do { if (!(_cond)) { kprintf("KASSERT: %s\n", (_msg)); } } while (0)

#endif
