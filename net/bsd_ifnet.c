#include "bsd_ifnet.h"
#include "../fabric/spin.h"
#include "../../include/common.h"
#include <stddef.h>

static bsd_ifnet_t* g_ifnets[BSD_IFNET_MAX];
static uint32_t g_ifnet_count = 0;
static spinlock_t g_ifnet_lock;
static int g_ifnet_inited = 0;

static void bsd_ifnet_init_once(void)
{
    if (g_ifnet_inited) {
        return;
    }
    spinlock_init(&g_ifnet_lock);
    g_ifnet_inited = 1;
}

int bsd_ifnet_attach(bsd_ifnet_t* ifp)
{
    if (!ifp || ifp->if_xname[0] == '\0') {
        return -1;
    }
    bsd_ifnet_init_once();

    spinlock_lock(&g_ifnet_lock);

    for (uint32_t i = 0; i < g_ifnet_count; i++) {
        if (strncmp(g_ifnets[i]->if_xname, ifp->if_xname, BSD_IFNAMSIZ) == 0) {
            spinlock_unlock(&g_ifnet_lock);
            return -1;
        }
    }

    if (g_ifnet_count >= BSD_IFNET_MAX) {
        spinlock_unlock(&g_ifnet_lock);
        return -1;
    }

    ifp->if_index = g_ifnet_count + 1u;
    g_ifnets[g_ifnet_count++] = ifp;

    spinlock_unlock(&g_ifnet_lock);
    return 0;
}

bsd_ifnet_t* bsd_ifnet_byindex(uint32_t ifindex)
{
    bsd_ifnet_t* out = NULL;
    if (ifindex == 0) {
        return NULL;
    }
    bsd_ifnet_init_once();

    spinlock_lock(&g_ifnet_lock);
    if (ifindex <= g_ifnet_count) {
        out = g_ifnets[ifindex - 1u];
    }
    spinlock_unlock(&g_ifnet_lock);
    return out;
}

bsd_ifnet_t* bsd_ifnet_byname(const char* ifname)
{
    if (!ifname || ifname[0] == '\0') {
        return NULL;
    }

    bsd_ifnet_t* out = NULL;
    bsd_ifnet_init_once();

    spinlock_lock(&g_ifnet_lock);
    for (uint32_t i = 0; i < g_ifnet_count; i++) {
        if (strncmp(g_ifnets[i]->if_xname, ifname, BSD_IFNAMSIZ) == 0) {
            out = g_ifnets[i];
            break;
        }
    }
    spinlock_unlock(&g_ifnet_lock);

    return out;
}
