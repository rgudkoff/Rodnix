#ifndef _RODNIX_COMPAT_NETINET_IP_ICMP_H
#define _RODNIX_COMPAT_NETINET_IP_ICMP_H

#include "../../kernel/net/bsd_inet.h"

typedef bsd_icmp_echo_t icmp;

#define ICMP_ECHOREPLY BSD_ICMP_ECHOREPLY
#define ICMP_ECHO BSD_ICMP_ECHO

#endif /* _RODNIX_COMPAT_NETINET_IP_ICMP_H */
