#include "bsd_ether.h"
#include "../../include/common.h"

#define BSD_ARP_CACHE_MAX 16

typedef struct bsd_arp_entry {
    uint32_t ip_host;
    uint8_t mac[BSD_ETHER_ADDR_LEN];
    uint8_t used;
} bsd_arp_entry_t;

static bsd_arp_entry_t g_arp_cache[BSD_ARP_CACHE_MAX];

void bsd_arp_init(void)
{
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
}

int bsd_arp_add(uint32_t ip_host, const uint8_t mac[BSD_ETHER_ADDR_LEN])
{
    if (!mac) {
        return -1;
    }

    for (int i = 0; i < BSD_ARP_CACHE_MAX; i++) {
        if (g_arp_cache[i].used && g_arp_cache[i].ip_host == ip_host) {
            memcpy(g_arp_cache[i].mac, mac, BSD_ETHER_ADDR_LEN);
            return 0;
        }
    }
    for (int i = 0; i < BSD_ARP_CACHE_MAX; i++) {
        if (!g_arp_cache[i].used) {
            g_arp_cache[i].used = 1u;
            g_arp_cache[i].ip_host = ip_host;
            memcpy(g_arp_cache[i].mac, mac, BSD_ETHER_ADDR_LEN);
            return 0;
        }
    }
    return -1;
}

int bsd_arp_lookup(uint32_t ip_host, uint8_t mac_out[BSD_ETHER_ADDR_LEN])
{
    if (!mac_out) {
        return -1;
    }
    for (int i = 0; i < BSD_ARP_CACHE_MAX; i++) {
        if (g_arp_cache[i].used && g_arp_cache[i].ip_host == ip_host) {
            memcpy(mac_out, g_arp_cache[i].mac, BSD_ETHER_ADDR_LEN);
            return 0;
        }
    }
    return -1;
}
