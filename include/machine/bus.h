#ifndef _RODNIX_COMPAT_MACHINE_BUS_H
#define _RODNIX_COMPAT_MACHINE_BUS_H
#include <sys/types.h>
#include <stdint.h>
typedef uintptr_t bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uintptr_t bus_dma_tag_t;
typedef uintptr_t bus_dmamap_t;

static inline volatile uint8_t* bus_space_ptr(bus_space_handle_t h, bus_size_t o)
{
    return (volatile uint8_t*)(uintptr_t)(h + (uintptr_t)o);
}

static inline uint8_t bus_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t;
    if (h == 0) {
        return 0;
    }
    return *bus_space_ptr(h, o);
}

static inline uint16_t bus_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t;
    if (h == 0) {
        return 0;
    }
    return *(volatile uint16_t*)(const void*)bus_space_ptr(h, o);
}

static inline uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t;
    if (h == 0) {
        return 0;
    }
    return *(volatile uint32_t*)(const void*)bus_space_ptr(h, o);
}

static inline void bus_space_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint8_t v)
{
    (void)t;
    if (h == 0) {
        return;
    }
    *bus_space_ptr(h, o) = v;
}

static inline void bus_space_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint16_t v)
{
    (void)t;
    if (h == 0) {
        return;
    }
    *(volatile uint16_t*)(const void*)bus_space_ptr(h, o) = v;
}

static inline void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint32_t v)
{
    (void)t;
    if (h == 0) {
        return;
    }
    *(volatile uint32_t*)(const void*)bus_space_ptr(h, o) = v;
}
#endif
