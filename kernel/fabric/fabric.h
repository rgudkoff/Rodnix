/**
 * @file fabric.h
 * @brief Fabric core interface
 * 
 * Fabric represents hardware as a managed graph.
 * It manages device lifecycle and driver matching.
 */

#ifndef _RODNIX_FABRIC_H
#define _RODNIX_FABRIC_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct fabric_bus    fabric_bus_t;
typedef struct fabric_device fabric_device_t;
typedef struct fabric_driver fabric_driver_t;
typedef struct fabric_service fabric_service_t;

/* Device matching structure */
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
} fabric_match_t;

/* Initialize Fabric */
void fabric_init(void);

/* Bus registration */
int fabric_bus_register(fabric_bus_t *bus);

/* Driver registration */
int fabric_driver_register(fabric_driver_t *driver);

/* Device publication */
int fabric_device_publish(fabric_device_t *device);

/* Service publication and lookup */
int fabric_service_publish(fabric_service_t *service);
fabric_service_t* fabric_service_lookup(const char *name);

/* IRQ integration */
typedef void (*fabric_irq_handler_t)(int vector, void *arg);
int  fabric_request_irq(int vector, fabric_irq_handler_t h, void *arg);
void fabric_free_irq(int vector, fabric_irq_handler_t h);

/* Logging */
void fabric_log(const char *fmt, ...);

#endif /* _RODNIX_FABRIC_H */

