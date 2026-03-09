#ifndef _RODNIX_USERLAND_NETINET_IN_H
#define _RODNIX_USERLAND_NETINET_IN_H

#include <sys/socket.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    uint8_t sin_len;
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    uint8_t sin_zero[8];
};

#define INADDR_ANY       ((in_addr_t)0x00000000u)
#define INADDR_BROADCAST ((in_addr_t)0xFFFFFFFFu)
#define INADDR_LOOPBACK  ((in_addr_t)0x7F000001u)

#endif /* _RODNIX_USERLAND_NETINET_IN_H */
