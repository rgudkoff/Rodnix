/**
 * @file e1000_net_disabled.c
 * @brief Build-time stub used while the shared e1000 sources are disabled.
 */

#include "../../../kernel/fabric/fabric.h"

void e1000_net_stub_init(void)
{
    fabric_log("[E1000] Shared e1000 driver set disabled at build time\n");
}
