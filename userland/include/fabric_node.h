#ifndef _RODNIX_USERLAND_FABRIC_NODE_H
#define _RODNIX_USERLAND_FABRIC_NODE_H

#include <stdint.h>

#define FABRIC_NODE_PATH_MAX 96u
#define FABRIC_NODE_NAME_MAX 32u
#define FABRIC_NODE_TYPE_MAX 16u
#define FABRIC_NODE_CLASS_MAX 16u

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

#endif /* _RODNIX_USERLAND_FABRIC_NODE_H */
