#ifndef _RODNIX_USERLAND_ARPA_INET_H
#define _RODNIX_USERLAND_ARPA_INET_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

uint16_t htons(uint16_t host16);
uint16_t ntohs(uint16_t net16);
uint32_t htonl(uint32_t host32);
uint32_t ntohl(uint32_t net32);

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size);
char* inet_ntoa(struct in_addr in);
char* inet_ntoa_r(struct in_addr in, char* buf, socklen_t size);

#endif /* _RODNIX_USERLAND_ARPA_INET_H */
