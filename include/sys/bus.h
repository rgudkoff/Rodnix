#ifndef _RODNIX_COMPAT_SYS_BUS_H
#define _RODNIX_COMPAT_SYS_BUS_H

#include <stdint.h>
#include "../../kernel/fabric/device/device.h"

/* Opaque driver-facing handles for the compatibility layer. */
typedef fabric_device_t* device_t;
typedef struct driver driver_t;
typedef void* devclass_t;

typedef int (*device_probe_t)(device_t dev);
typedef int (*device_attach_t)(device_t dev);
typedef int (*device_detach_t)(device_t dev);

struct driver {
    const char* name;
    void* methods;
    uint32_t size;
};

typedef struct {
    const char* name;
    void* func;
} device_method_t;

#define DEVMETHOD(_name, _func) { #_name, (void*)(_func) }
#define DEVMETHOD_END { 0, 0 }

/* Minimal unit/description stubs often used by ports. */
static inline int device_get_unit(device_t dev)
{
    (void)dev;
    return 0;
}

static inline const char* device_get_desc(device_t dev)
{
    return dev ? dev->name : "";
}

#endif /* _RODNIX_COMPAT_SYS_BUS_H */
