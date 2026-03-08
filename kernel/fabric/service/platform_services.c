/**
 * @file platform_services.c
 * @brief Fabric platform services bootstrap (clock/timer/serial/console)
 */

#include "../fabric.h"
#include "service.h"
#include "../../../include/error.h"
#include "../../../include/console.h"

typedef struct {
    rdnx_abi_header_t hdr;
} fabric_platform_ops_t;

static fabric_platform_ops_t g_clock_ops = {
    .hdr = RDNX_ABI_INIT(fabric_platform_ops_t)
};
static fabric_platform_ops_t g_timer_ops = {
    .hdr = RDNX_ABI_INIT(fabric_platform_ops_t)
};
static fabric_platform_ops_t g_serial_ops = {
    .hdr = RDNX_ABI_INIT(fabric_platform_ops_t)
};
static fabric_platform_ops_t g_console_ops = {
    .hdr = RDNX_ABI_INIT(fabric_platform_ops_t)
};

static fabric_service_t g_clock_service = {
    .hdr = RDNX_ABI_INIT(fabric_service_t),
    .name = "clock0",
    .ops = &g_clock_ops,
    .context = NULL
};
static fabric_service_t g_timer_service = {
    .hdr = RDNX_ABI_INIT(fabric_service_t),
    .name = "timer0",
    .ops = &g_timer_ops,
    .context = NULL
};
static fabric_service_t g_serial_service = {
    .hdr = RDNX_ABI_INIT(fabric_service_t),
    .name = "serial0",
    .ops = &g_serial_ops,
    .context = NULL
};
static fabric_service_t g_console_service = {
    .hdr = RDNX_ABI_INIT(fabric_service_t),
    .name = "console0",
    .ops = &g_console_ops,
    .context = NULL
};

static void publish_platform_service(fabric_service_t* svc, const char* kind)
{
    if (fabric_service_publish(svc) != RDNX_OK) {
        return;
    }
    if (fabric_publish_service_node(svc->name, kind, NULL) != RDNX_OK) {
        return;
    }

    char path[FABRIC_NODE_PATH_MAX];
    path[0] = '\0';
    {
        uint32_t i = 0;
        const char* p = "/fabric/services/";
        while (p[i] && i + 1 < sizeof(path)) {
            path[i] = p[i];
            i++;
        }
        uint32_t j = 0;
        while (svc->name[j] && i + 1 < sizeof(path)) {
            path[i++] = svc->name[j++];
        }
        path[i] = '\0';
    }
    (void)fabric_node_set_state(path, FABRIC_STATE_ACTIVE);
}

void fabric_platform_services_init(void)
{
    publish_platform_service(&g_clock_service, "clock");
    publish_platform_service(&g_timer_service, "timer");
    publish_platform_service(&g_serial_service, "serial");
    publish_platform_service(&g_console_service, "console");
    kputs("[FABRIC] platform services initialized\n");
}
