#ifndef _RODNIX_USERLAND_ASSERT_H
#define _RODNIX_USERLAND_ASSERT_H

#include <stdlib.h>

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : abort())
#endif

#endif /* _RODNIX_USERLAND_ASSERT_H */
