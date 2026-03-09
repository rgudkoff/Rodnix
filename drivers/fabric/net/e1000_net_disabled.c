/**
 * @file e1000_net_disabled.c
 * @brief Build-time stub used while FreeBSD e1000 sources are disabled.
 */

#include "../../../kernel/fabric/fabric.h"

void e1000_net_stub_init(void)
{
    fabric_log("[E1000] FreeBSD driver set disabled at build time\n");
}
