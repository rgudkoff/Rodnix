#ifndef _RODNIX_COMPAT_SYS_MODULE_H
#define _RODNIX_COMPAT_SYS_MODULE_H

#include <stdint.h>

/* Module event constants for the compatibility layer. */
#define MOD_LOAD 1
#define MOD_UNLOAD 2
#define MOD_SHUTDOWN 3

typedef int (*modeventhand_t)(void* mod, int what, void* arg);

typedef struct moduledata {
    const char* name;
    modeventhand_t evhand;
    void* priv;
} moduledata_t;

/* Registration macros are no-ops in the current RodNIX compatibility layer. */
#define DECLARE_MODULE(_name, _data, _sub, _order)
#define MODULE_VERSION(_name, _ver)
#define MODULE_DEPEND(_name, _dep, _min, _pref, _max)
#define DRIVER_MODULE(_name, _bus, _driver, _devclass, _evh, _arg)

#endif /* _RODNIX_COMPAT_SYS_MODULE_H */
