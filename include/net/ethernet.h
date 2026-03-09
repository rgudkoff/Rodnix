#ifndef _RODNIX_COMPAT_NET_ETHERNET_H
#define _RODNIX_COMPAT_NET_ETHERNET_H

#include "../../kernel/net/bsd_ether.h"

typedef bsd_ether_header_t ether_header;

#define ETHER_ADDR_LEN BSD_ETHER_ADDR_LEN
#define ETHERTYPE_IP BSD_ETHERTYPE_IP
#define ETHERTYPE_ARP BSD_ETHERTYPE_ARP

#endif /* _RODNIX_COMPAT_NET_ETHERNET_H */
