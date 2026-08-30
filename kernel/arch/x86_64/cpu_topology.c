/**
 * @file cpu_topology.c
 * @brief Processor inventory built from ACPI MADT.
 */

#include "cpu_topology.h"
#include "acpi.h"
#include "apic.h"
#include "../../../include/common.h"
#include "../../../include/console.h"
#include "../../../trace/bootlog.h"

static struct cpu_topology_entry g_cpus[X86_64_MAX_CPUS];
static uint32_t g_cpu_count = 0;
static uint32_t g_bsp_index = 0;
static bool g_ready = false;
static bool g_truncated = false;

/* MADT may describe one processor twice when firmware emits both a type 0 and
 * a type 9 entry for it. The APIC ID is what addresses a processor, so it is
 * what identity is decided on. */
static int topology_find_apic_id(uint32_t apic_id)
{
    for (uint32_t i = 0; i < g_cpu_count; i++) {
        if (g_cpus[i].apic_id == apic_id) {
            return (int)i;
        }
    }
    return -1;
}

static bool topology_append(const struct acpi_madt_cpu_info* info)
{
    if (g_cpu_count >= X86_64_MAX_CPUS) {
        g_truncated = true;
        return false;
    }

    struct cpu_topology_entry* e = &g_cpus[g_cpu_count];
    e->apic_id = info->apic_id;
    e->acpi_uid = info->acpi_uid;
    e->enabled = (info->flags & ACPI_MADT_CPU_ENABLED) != 0;
    e->online_capable = (info->flags & ACPI_MADT_CPU_ONLINE_CAPABLE) != 0;
    e->x2apic = info->x2apic;
    e->bsp = false;
    g_cpu_count++;
    return true;
}

/* Used when ACPI is absent or describes no usable processor: the machine
 * still has the one processor executing this code. */
static void topology_synthesise_uniprocessor(void)
{
    g_cpus[0].apic_id = apic_get_lapic_id_ext();
    g_cpus[0].acpi_uid = 0;
    g_cpus[0].enabled = true;
    g_cpus[0].online_capable = false;
    g_cpus[0].x2apic = false;
    g_cpus[0].bsp = true;
    g_cpu_count = 1;
    g_bsp_index = 0;
}

int cpu_topology_init(void)
{
    if (g_ready) {
        return 0;
    }

    g_cpu_count = 0;
    g_bsp_index = 0;
    g_truncated = false;

    struct acpi_madt_cpu_info entries[X86_64_MAX_CPUS];
    uint32_t total = 0;
    uint32_t written = 0;

    if (acpi_madt_get_cpus(NULL, 0, &total) != 0) {
        klog_warn("cpu", "MADT unavailable, assuming uniprocessor\n");
        topology_synthesise_uniprocessor();
        g_ready = true;
        return 0;
    }

    if (acpi_madt_get_cpus(entries, X86_64_MAX_CPUS, &written) != 0) {
        klog_warn("cpu", "MADT processor list unreadable, assuming uniprocessor\n");
        topology_synthesise_uniprocessor();
        g_ready = true;
        return 0;
    }

    if (written < total) {
        g_truncated = true;
    }

    for (uint32_t i = 0; i < written; i++) {
        const struct acpi_madt_cpu_info* info = &entries[i];

        /* Neither Enabled nor Online Capable: the processor can never be
         * brought online, so it is not part of the inventory. */
        if ((info->flags &
             (ACPI_MADT_CPU_ENABLED | ACPI_MADT_CPU_ONLINE_CAPABLE)) == 0) {
            continue;
        }

        int dup = topology_find_apic_id(info->apic_id);
        if (dup >= 0) {
            /* Keep the wider description: a type 9 entry carries the full
             * 32-bit ID and the 32-bit ACPI UID. */
            if (info->x2apic) {
                g_cpus[dup].x2apic = true;
                g_cpus[dup].acpi_uid = info->acpi_uid;
            }
            continue;
        }

        if (!topology_append(info)) {
            break;
        }
    }

    if (g_cpu_count == 0) {
        klog_warn("cpu", "MADT lists no usable processor, assuming uniprocessor\n");
        topology_synthesise_uniprocessor();
        g_ready = true;
        return 0;
    }

    uint32_t bsp_apic_id = apic_get_lapic_id_ext();
    int bsp = topology_find_apic_id(bsp_apic_id);
    if (bsp < 0) {
        /* The running processor is not in its own MADT. Trust the hardware
         * over the table and keep index 0 as the boot processor, but say so:
         * an AP bring-up built on a table this wrong would fail obscurely. */
        klog_warn("cpu", "BSP APIC ID %u absent from MADT, using index 0\n",
                (unsigned)bsp_apic_id);
        bsp = 0;
    }
    g_bsp_index = (uint32_t)bsp;
    g_cpus[g_bsp_index].bsp = true;

    g_ready = true;
    return 0;
}

bool cpu_topology_ready(void)
{
    return g_ready;
}

uint32_t cpu_topology_count(void)
{
    return g_ready ? g_cpu_count : 1;
}

uint32_t cpu_topology_startable_count(void)
{
    if (!g_ready) {
        return 1;
    }

    uint32_t n = 0;
    for (uint32_t i = 0; i < g_cpu_count; i++) {
        if (g_cpus[i].enabled) {
            n++;
        }
    }
    return n;
}

const struct cpu_topology_entry* cpu_topology_get(uint32_t index)
{
    if (!g_ready || index >= g_cpu_count) {
        return NULL;
    }
    return &g_cpus[index];
}

uint32_t cpu_topology_bsp_index(void)
{
    return g_ready ? g_bsp_index : 0;
}

int cpu_topology_index_for_apic_id(uint32_t apic_id)
{
    if (!g_ready) {
        return -1;
    }
    return topology_find_apic_id(apic_id);
}

void cpu_topology_report(void)
{
    if (!g_ready) {
        klog_warn("cpu", "topology not initialised\n");
        return;
    }

    klog("cpu", "%u processor(s), %u startable, BSP index %u\n",
            (unsigned)g_cpu_count,
            (unsigned)cpu_topology_startable_count(),
            (unsigned)g_bsp_index);

    if (g_truncated) {
        klog_warn("cpu", "MADT lists more than %u processors, table truncated\n",
                (unsigned)X86_64_MAX_CPUS);
    }

    for (uint32_t i = 0; i < g_cpu_count; i++) {
        const struct cpu_topology_entry* e = &g_cpus[i];
        klog("cpu", "  cpu%u apic_id=%u uid=%u %s%s%s\n",
                (unsigned)i,
                (unsigned)e->apic_id,
                (unsigned)e->acpi_uid,
                e->enabled ? "enabled" : "online-capable",
                e->x2apic ? " x2apic" : "",
                e->bsp ? " BSP" : "");
    }
}
