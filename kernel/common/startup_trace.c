/**
 * @file startup_trace.c
 * @brief FreeBSD-inspired startup trace (bootverbose + verbose_sysinit).
 */

#include "startup_trace.h"
#include "bootlog.h"
#include "../../include/console.h"
#include "../../include/common.h"
#include <stdbool.h>

static bool g_bootverbose = false;
static bool g_verbose_sysinit = false;

static bool cmdline_has_substr(const char* cmdline, const char* needle)
{
    if (!cmdline || !needle || needle[0] == '\0') {
        return false;
    }
    return strstr(cmdline, needle) != NULL;
}

void startup_trace_init(const char* cmdline)
{
    g_bootverbose = false;
    g_verbose_sysinit = false;

    if (!cmdline) {
        return;
    }

    if (cmdline_has_substr(cmdline, "bootverbose") ||
        cmdline_has_substr(cmdline, "debug.bootverbose=1") ||
        cmdline_has_substr(cmdline, "bootlog=verbose") ||
        cmdline_has_substr(cmdline, "startup_debug=verbose")) {
        g_bootverbose = true;
    }
    if (cmdline_has_substr(cmdline, "verbose_sysinit") ||
        cmdline_has_substr(cmdline, "debug.verbose_sysinit=1")) {
        g_verbose_sysinit = true;
    }
}

int startup_trace_bootverbose(void)
{
    return g_bootverbose ? 1 : 0;
}

int startup_trace_verbose_sysinit(void)
{
    return g_verbose_sysinit ? 1 : 0;
}

static bool startup_trace_is_enabled(void)
{
    return g_bootverbose || g_verbose_sysinit;
}

void startup_trace_step_begin(uint32_t subsystem, uint32_t order, const char* name)
{
    if (!startup_trace_is_enabled()) {
        return;
    }

    if (!name) {
        name = "unnamed";
    }
    kprintf("SYSINIT: sub=0x%08x order=0x%08x %s ...\n",
            (unsigned)subsystem, (unsigned)order, name);
    bootlog_mark("startup", "enter");
}

void startup_trace_step_end(uint32_t subsystem, uint32_t order, const char* name, int rc)
{
    (void)subsystem;
    (void)order;
    if (!startup_trace_is_enabled()) {
        return;
    }

    if (!name) {
        name = "unnamed";
    }
    if (rc == 0) {
        kprintf("SYSINIT: %s done\n", name);
        bootlog_mark("startup", "done");
    } else {
        kprintf("SYSINIT: %s failed rc=%d\n", name, rc);
        bootlog_mark("startup", "fail");
    }
}
