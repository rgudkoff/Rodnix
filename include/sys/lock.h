#ifndef _RODNIX_COMPAT_SYS_LOCK_H
#define _RODNIX_COMPAT_SYS_LOCK_H
#define SX_XLOCKED 1
#define sx_assert(_lock, _what) do { (void)(_lock); (void)(_what); } while (0)
#endif
