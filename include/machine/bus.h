#ifndef _RODNIX_COMPAT_MACHINE_BUS_H
#define _RODNIX_COMPAT_MACHINE_BUS_H
#include <sys/types.h>
#include <stdint.h>
typedef uintptr_t bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uintptr_t bus_dma_tag_t;
typedef uintptr_t bus_dmamap_t;

static inline uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t; (void)h; (void)o;
    return 0;
}

static inline void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint32_t v)
{
    (void)t; (void)h; (void)o; (void)v;
}
#endif
