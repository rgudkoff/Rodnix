#ifndef _RODNIX_BSD_NETISR_H
#define _RODNIX_BSD_NETISR_H

#include <stdint.h>
#include "bsd_mbuf.h"

#define BSD_NETISR_MAX 16
#define BSD_NETISR_QDEPTH 64

/* Keep protocol ids aligned with FreeBSD netisr.h subset. */
#define BSD_NETISR_IP 1u

typedef void (*bsd_netisr_handler_fn_t)(bsd_mbuf_t* m);

typedef struct bsd_netisr_handler {
    const char* nh_name;
    bsd_netisr_handler_fn_t nh_handler;
    uint32_t nh_proto;
    uint32_t nh_qlimit;
} bsd_netisr_handler_t;

int bsd_netisr_init(void);
int bsd_netisr_register(const bsd_netisr_handler_t* nh);
int bsd_netisr_dispatch(uint32_t proto, bsd_mbuf_t* m);
int bsd_netisr_queue(uint32_t proto, bsd_mbuf_t* m);

/* FreeBSD-style aliases for faster driver porting. */
typedef bsd_netisr_handler_t netisr_handler_t;

#define NETISR_IP BSD_NETISR_IP
#define netisr_register(_nh) bsd_netisr_register((_nh))
#define netisr_dispatch(_proto, _m) bsd_netisr_dispatch((_proto), (_m))
#define netisr_queue(_proto, _m) bsd_netisr_queue((_proto), (_m))

#endif /* _RODNIX_BSD_NETISR_H */
