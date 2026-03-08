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

typedef struct {
    uint32_t buses;
    uint32_t drivers;
    uint32_t devices;
    uint32_t services;
} fabric_stats_t;

int fabric_get_stats(fabric_stats_t* out);
uint32_t fabric_device_count(void);
fabric_device_t* fabric_device_get(uint32_t index);

#define FABRIC_NODE_PATH_MAX 96u
#define FABRIC_NODE_NAME_MAX 32u
#define FABRIC_NODE_TYPE_MAX 16u
#define FABRIC_NODE_CLASS_MAX 16u
#define FABRIC_EVENT_SUBJECT_MAX 32u
#define FABRIC_EVENT_DETAIL_MAX 32u

typedef struct fabric_node_info {
    uint64_t id;
    char path[FABRIC_NODE_PATH_MAX];
    char name[FABRIC_NODE_NAME_MAX];
    char type[FABRIC_NODE_TYPE_MAX];
    char class_name[FABRIC_NODE_CLASS_MAX];
    char provider_path[FABRIC_NODE_PATH_MAX];
    char driver[FABRIC_NODE_NAME_MAX];
    uint32_t state;
    uint32_t flags;
} fabric_node_info_t;

uint32_t fabric_node_count(void);
int fabric_node_get_info(uint32_t index, fabric_node_info_t* out);
int fabric_node_list(fabric_node_info_t* out, uint32_t max_entries, uint32_t* out_total);
int fabric_node_set_state(const char* path, uint32_t state);
int fabric_publish_netif_node(const char* ifname, fabric_device_t* provider_dev);
int fabric_publish_input_node(const char* input_name, fabric_device_t* provider_dev);

typedef enum fabric_state {
    FABRIC_STATE_UNSPEC = 0,
    FABRIC_STATE_INIT = 1,
    FABRIC_STATE_DISCOVERED = 2,
    FABRIC_STATE_ATTACHED = 3,
    FABRIC_STATE_PUBLISHED = 4,
    FABRIC_STATE_REGISTERED = 5,
    FABRIC_STATE_ACTIVE = 6,
    FABRIC_STATE_ERROR = 7,
    FABRIC_STATE_REMOVED = 8
} fabric_state_t;

typedef enum fabric_event_type {
    FABRIC_EVENT_DEVICE_ADDED = 1,
    FABRIC_EVENT_DRIVER_ATTACHED = 2,
    FABRIC_EVENT_SERVICE_PUBLISHED = 3,
    FABRIC_EVENT_SERVICE_REGISTERED = 4,
    FABRIC_EVENT_STATE_CHANGED = 5
} fabric_event_type_t;

typedef struct fabric_event {
    uint64_t seq;
    uint32_t type;
    uint32_t flags;
    char node_path[FABRIC_NODE_PATH_MAX];
    char subject[FABRIC_EVENT_SUBJECT_MAX];
    char detail[FABRIC_EVENT_DETAIL_MAX];
} fabric_event_t;

typedef void (*fabric_event_handler_t)(const fabric_event_t* event, void* arg);

int fabric_event_subscribe(fabric_event_handler_t handler, void* arg);
void fabric_event_unsubscribe(fabric_event_handler_t handler, void* arg);
int fabric_event_drain(fabric_event_t* out, uint32_t max_entries, uint32_t* out_read, uint32_t* out_dropped);

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
