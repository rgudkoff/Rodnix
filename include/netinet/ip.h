#ifndef _RODNIX_COMPAT_NETINET_IP_H
#define _RODNIX_COMPAT_NETINET_IP_H

#include "../../kernel/net/bsd_inet.h"

typedef bsd_ip_t ip;

#define IPVERSION BSD_IPVERSION
#define IP_DF BSD_IP_DF
#define IPPROTO_ICMP BSD_IPPROTO_ICMP
#define IPPROTO_UDP BSD_IPPROTO_UDP

#endif /* _RODNIX_COMPAT_NETINET_IP_H */
