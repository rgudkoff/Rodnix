/**
 * @file net_service.c
 * @brief Fabric network service implementation
 */

#include "net_service.h"
#include "../fabric.h"
#include "../spin.h"
#include "service.h"
#include "../../../include/common.h"
#include "../../../include/error.h"

typedef struct {
    rdnx_abi_header_t hdr;
    int (*register_iface)(fabric_netif_t* iface);
    uint32_t (*iface_count)(void);
    fabric_netif_t* (*iface_get)(uint32_t index);
    int (*tx)(fabric_netif_t* iface, const void* frame, uint32_t len);
    int (*submit_rx)(fabric_netif_t* iface, const void* frame, uint32_t len);
} fabric_net_service_ops_t;

static spinlock_t g_net_lock;
static int g_net_inited = 0;
static int g_event_subscribed = 0;
static fabric_netif_t* g_ifaces[FABRIC_NETIF_MAX];
static uint32_t g_iface_count = 0;

static void net_interface_manager_event(const fabric_event_t* event, void* arg)
{
    (void)arg;
    if (!event) {
        return;
    }
    if (event->type != FABRIC_EVENT_SERVICE_REGISTERED) {
        return;
    }
    if (strcmp(event->detail, "net") != 0) {
        return;
    }
    if (!event->subject[0]) {
        return;
    }

    spinlock_lock(&g_net_lock);
    for (uint32_t i = 0; i < g_iface_count; i++) {
        fabric_netif_t* iface = g_ifaces[i];
        if (!iface || !iface->name) {
            continue;
        }
        if (strcmp(iface->name, event->subject) != 0) {
            continue;
        }
        iface->flags |= FABRIC_NETIF_F_UP;
        spinlock_unlock(&g_net_lock);
        (void)fabric_node_set_state(event->node_path, FABRIC_STATE_ACTIVE);
        fabric_log("[ifmgr] auto-init net interface: %s\n", event->subject);
        return;
    }
    spinlock_unlock(&g_net_lock);
}

static int net_service_register_iface(fabric_netif_t* iface)
{
    return fabric_netif_register(iface);
}

static uint32_t net_service_iface_count(void)
{
    return fabric_netif_count();
}

static fabric_netif_t* net_service_iface_get(uint32_t index)
{
    return fabric_netif_get(index);
}

static int net_service_tx(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    return fabric_netif_tx(iface, frame, len);
}

static int net_service_submit_rx(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    return fabric_netif_rx_submit(iface, frame, len);
}

static fabric_net_service_ops_t g_ops = {
    .hdr = RDNX_ABI_INIT(fabric_net_service_ops_t),
    .register_iface = net_service_register_iface,
    .iface_count = net_service_iface_count,
    .iface_get = net_service_iface_get,
    .tx = net_service_tx,
    .submit_rx = net_service_submit_rx
};

static fabric_service_t g_service = {
    .hdr = RDNX_ABI_INIT(fabric_service_t),
    .name = "net.ifmgr",
    .ops = &g_ops,
    .context = NULL
};

int fabric_net_service_init(void)
{
    if (g_net_inited) {
        return RDNX_OK;
    }
    spinlock_init(&g_net_lock);
    for (uint32_t i = 0; i < FABRIC_NETIF_MAX; i++) {
        g_ifaces[i] = NULL;
    }
    g_iface_count = 0;
    if (!g_event_subscribed) {
        if (fabric_event_subscribe(net_interface_manager_event, NULL) == RDNX_OK) {
            g_event_subscribed = 1;
        }
    }
    if (fabric_service_publish(&g_service) != 0) {
        return RDNX_E_GENERIC;
    }
    (void)fabric_node_set_state("/fabric/subsystems/net/ifmgr", FABRIC_STATE_ACTIVE);
    g_net_inited = 1;
    fabric_log("[fabric-net] service ready: %s\n", g_service.name);
    return RDNX_OK;
}

int fabric_netif_register(fabric_netif_t* iface)
{
    if (!g_net_inited || !iface || !iface->name) {
        return RDNX_E_INVALID;
    }
    if (iface->hdr.abi_version != RDNX_ABI_VERSION ||
        iface->hdr.size < sizeof(fabric_netif_t)) {
        return RDNX_E_INVALID;
    }

    spinlock_lock(&g_net_lock);
    if (g_iface_count >= FABRIC_NETIF_MAX) {
        spinlock_unlock(&g_net_lock);
        return RDNX_E_BUSY;
    }
    for (uint32_t i = 0; i < g_iface_count; i++) {
        if (g_ifaces[i] && strcmp(g_ifaces[i]->name, iface->name) == 0) {
            spinlock_unlock(&g_net_lock);
            return RDNX_E_BUSY;
        }
    }
    g_ifaces[g_iface_count++] = iface;
    spinlock_unlock(&g_net_lock);
    fabric_log("[fabric-net] iface registered: %s mtu=%u\n",
               iface->name, iface->mtu);
    return RDNX_OK;
}

uint32_t fabric_netif_count(void)
{
    spinlock_lock(&g_net_lock);
    uint32_t count = g_iface_count;
    spinlock_unlock(&g_net_lock);
    return count;
}

fabric_netif_t* fabric_netif_get(uint32_t index)
{
    fabric_netif_t* iface = NULL;
    spinlock_lock(&g_net_lock);
    if (index < g_iface_count) {
        iface = g_ifaces[index];
    }
    spinlock_unlock(&g_net_lock);
    return iface;
}

int fabric_netif_tx(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    if (!iface || !frame || len == 0 || len > FABRIC_NET_FRAME_MAX) {
        return RDNX_E_INVALID;
    }
    if ((iface->flags & FABRIC_NETIF_F_UP) == 0) {
        iface->stats.drops++;
        return RDNX_E_DENIED;
    }
    if (!iface->ops || !iface->ops->tx) {
        iface->stats.drops++;
        return RDNX_E_UNSUPPORTED;
    }
    int ret = iface->ops->tx(iface, frame, len);
    if (ret == RDNX_OK || ret == 0) {
        iface->stats.tx_frames++;
        iface->stats.tx_bytes += len;
        return RDNX_OK;
    }
    iface->stats.drops++;
    return ret;
}

int fabric_netif_rx_submit(fabric_netif_t* iface, const void* frame, uint32_t len)
{
    if (!iface || !frame || len == 0 || len > FABRIC_NET_FRAME_MAX) {
        return RDNX_E_INVALID;
    }
    if ((iface->flags & FABRIC_NETIF_F_UP) == 0) {
        iface->stats.drops++;
        return RDNX_E_DENIED;
    }
    iface->stats.rx_frames++;
    iface->stats.rx_bytes += len;
    return RDNX_OK;
}

int fabric_netif_get_info(uint32_t index, fabric_netif_info_t* out)
{
    if (!out) {
        return RDNX_E_INVALID;
    }
    memset(out, 0, sizeof(*out));
    fabric_netif_t* iface = fabric_netif_get(index);
    if (!iface || !iface->name) {
        return RDNX_E_NOTFOUND;
    }
    strncpy(out->name, iface->name, sizeof(out->name) - 1);
    memcpy(out->mac, iface->mac, sizeof(out->mac));
    out->mtu = iface->mtu;
    out->flags = iface->flags;
    out->ipv4_addr = iface->ipv4_addr;
    out->ipv4_netmask = iface->ipv4_netmask;
    out->ipv4_gateway = iface->ipv4_gateway;
    out->stats = iface->stats;
    return RDNX_OK;
}

void fabric_netif_poll_all(void)
{
    spinlock_lock(&g_net_lock);
    uint32_t n = g_iface_count;
    fabric_netif_t* ifaces[FABRIC_NETIF_MAX];
    for (uint32_t i = 0; i < n && i < FABRIC_NETIF_MAX; i++) {
        ifaces[i] = g_ifaces[i];
    }
    spinlock_unlock(&g_net_lock);

    for (uint32_t i = 0; i < n && i < FABRIC_NETIF_MAX; i++) {
        fabric_netif_t* iface = ifaces[i];
        if (!iface || !iface->ops || !iface->ops->poll) {
            continue;
        }
        (void)iface->ops->poll(iface);
    }
}
