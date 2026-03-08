/*
 * Minimal cdefs compatibility layer for importing BSD headers into Rodnix.
 */

#ifndef _RODNIX_SYS_CDEFS_H_
#define _RODNIX_SYS_CDEFS_H_

#include <stddef.h>
#include <stdint.h>

#ifndef __uintptr_t
typedef uintptr_t __uintptr_t;
#endif

#ifndef __offsetof
#define __offsetof(type, field) ((size_t)&(((type*)0)->field))
#endif

#ifndef __CONCAT
#define __CONCAT1(x, y) x ## y
#define __CONCAT(x, y) __CONCAT1(x, y)
#endif

#ifndef __typeof
#define __typeof(x) __typeof__(x)
#endif

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#ifndef __used
#define __used __attribute__((used))
#endif

#ifndef __dead2
#define __dead2 __attribute__((noreturn))
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif

#ifndef __predict_true
#define __predict_true(exp) __builtin_expect(!!(exp), 1)
#endif

#ifndef __predict_false
#define __predict_false(exp) __builtin_expect(!!(exp), 0)
#endif

#ifndef __improbable
#define __improbable(exp) __predict_false(exp)
#endif

#ifndef __single
#define __single
#endif

#ifndef __unsafe_forge_single
#define __unsafe_forge_single(type, expr) ((type)(expr))
#endif

#endif /* _RODNIX_SYS_CDEFS_H_ */
