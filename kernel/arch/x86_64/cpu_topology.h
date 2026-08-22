/**
 * @file cpu_topology.h
 * @brief Processor inventory built from ACPI MADT.
 *
 * This is the single source of truth for "which processors exist and what
 * are their APIC IDs". It normalises the two MADT representations (type 0
 * Local APIC and type 9 Local x2APIC) into one table and assigns each
 * processor a dense index, which later SMP work uses to address per-CPU
 * state.
 *
 * The table is read-only after cpu_topology_init(); no processor is started
 * here. Bringing APs up is a separate step (see docs/ru/smp_bringup.md).
 */

#ifndef _RODNIX_ARCH_X86_64_CPU_TOPOLOGY_H
#define _RODNIX_ARCH_X86_64_CPU_TOPOLOGY_H

#include <stdbool.h>
#include <stdint.h>

/* Upper bound on processors tracked. Entries past this are reported as
 * truncation rather than silently dropped. */
#define X86_64_MAX_CPUS 64

struct cpu_topology_entry {
    uint32_t apic_id;        /* LAPIC / x2APIC ID, destination for IPIs */
    uint32_t acpi_uid;       /* ACPI processor UID, for firmware correlation */
    bool enabled;            /* MADT Enabled: may be started now */
    bool online_capable;     /* MADT Online Capable: hot-plug candidate */
    bool x2apic;             /* described by a type 9 entry */
    bool bsp;                /* the processor running cpu_topology_init() */
};

/* Build the table. Idempotent; safe to call before ACPI is available, in
 * which case a single-processor table is synthesised from the live LAPIC.
 * Returns 0 on success, -1 if the table could not be built at all. */
int cpu_topology_init(void);

bool cpu_topology_ready(void);

/* Number of processors in the table. Always >= 1 once initialised, and 1
 * before that, so callers never have to special-case zero. */
uint32_t cpu_topology_count(void);

/* Number of processors that MADT marks Enabled, i.e. startable now. */
uint32_t cpu_topology_startable_count(void);

/* NULL if index is out of range. */
const struct cpu_topology_entry* cpu_topology_get(uint32_t index);

/* Dense index of the boot processor. */
uint32_t cpu_topology_bsp_index(void);

/* Dense index for an APIC ID, or -1 if that ID is not in the table. */
int cpu_topology_index_for_apic_id(uint32_t apic_id);

/* Emit the inventory to the boot log. */
void cpu_topology_report(void);

#endif /* _RODNIX_ARCH_X86_64_CPU_TOPOLOGY_H */
