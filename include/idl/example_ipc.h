#ifndef _RODNIX_IDL_EXAMPLE_IPC_H
#define _RODNIX_IDL_EXAMPLE_IPC_H

#include <stdint.h>

/* Auto-generated IPC message ids. */
enum {
    DEMO_MSG_PING = 1,
};

/* Auto-generated request/reply structs. */
typedef struct demo_ping_request {
    uint32_t value;
} demo_ping_request_t;

typedef struct demo_ping_reply {
    uint32_t status;
} demo_ping_reply_t;

#endif /* _RODNIX_IDL_EXAMPLE_IPC_H */
