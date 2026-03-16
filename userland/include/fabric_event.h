#ifndef _RODNIX_USERLAND_FABRIC_EVENT_H
#define _RODNIX_USERLAND_FABRIC_EVENT_H

#include <stdint.h>
#include "fabric_node.h"

#define FABRIC_EVENT_SUBJECT_MAX 32u
#define FABRIC_EVENT_DETAIL_MAX 32u

enum {
    FABRIC_EVENT_DEVICE_ADDED = 1,
    FABRIC_EVENT_DRIVER_ATTACHED = 2,
    FABRIC_EVENT_SERVICE_PUBLISHED = 3,
    FABRIC_EVENT_SERVICE_REGISTERED = 4,
    FABRIC_EVENT_STATE_CHANGED = 5
};

typedef struct fabric_event {
    uint64_t seq;
    uint32_t type;
    uint32_t flags;
    char node_path[FABRIC_NODE_PATH_MAX];
    char subject[FABRIC_EVENT_SUBJECT_MAX];
    char detail[FABRIC_EVENT_DETAIL_MAX];
} fabric_event_t;

#endif /* _RODNIX_USERLAND_FABRIC_EVENT_H */
