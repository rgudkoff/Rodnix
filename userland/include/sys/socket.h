#ifndef _RODNIX_USERLAND_SYS_SOCKET_H
#define _RODNIX_USERLAND_SYS_SOCKET_H

#include <sys/types.h>

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr {
    uint8_t sa_len;
    sa_family_t sa_family;
    char sa_data[14];
};

#define AF_UNSPEC 0
#define AF_INET   2
#define AF_INET6  28

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#endif /* _RODNIX_USERLAND_SYS_SOCKET_H */
