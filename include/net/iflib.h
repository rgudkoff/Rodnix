#ifndef _RODNIX_COMPAT_NET_IFLIB_H
#define _RODNIX_COMPAT_NET_IFLIB_H

#include <stdint.h>
#include "if.h"
#include "../sys/bus.h"

/* Minimal iflib compatibility skeleton for incremental porting. */
typedef void* if_ctx_t;
typedef void* if_softc_ctx_t;

struct if_shared_ctx {
    uint32_t isc_magic;
    uint32_t isc_nrxqsets;
    uint32_t isc_ntxqsets;
    uint32_t isc_ntxd_default;
    uint32_t isc_nrxd_default;
};

typedef struct if_shared_ctx* if_shared_ctx_t;

#define IFLIB_MAGIC 0x49464c42u

static inline if_ctx_t iflib_get_ifp(if_ctx_t ctx)
{
    return ctx;
}

#endif /* _RODNIX_COMPAT_NET_IFLIB_H */
