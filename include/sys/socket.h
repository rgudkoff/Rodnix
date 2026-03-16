#ifndef _RODNIX_COMPAT_SYS_SOCKET_H
#define _RODNIX_COMPAT_SYS_SOCKET_H
#include <stdint.h>
#define AF_INET 2
#define AF_INET6 28
struct sockaddr { uint8_t sa_len; uint8_t sa_family; char sa_data[14]; };
#endif
