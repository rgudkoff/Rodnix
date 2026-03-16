/**
 * @file bus.h
 * @brief Fabric bus interface
 */

#ifndef _RODNIX_FABRIC_BUS_H
#define _RODNIX_FABRIC_BUS_H

typedef struct fabric_bus {
    const char *name;
    void (*enumerate)(void);   /* Publish devices to Fabric */
    void (*rescan)(void);      /* Optional: rescan bus */
} fabric_bus_t;

#endif /* _RODNIX_FABRIC_BUS_H */

