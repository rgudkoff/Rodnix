/**
 * @file bootlog.c
 * @brief XNU-inspired boot phase logging.
 *
 * Dual channel:
 * - always writes a compact event into debug event ring;
 * - optionally prints human-readable messages when verbose mode is enabled.
 */

#include "bootlog.h"
#include "../core/boot.h"
#include "../core/cpu.h"
#include "scheduler.h"
#include "../../include/common.h"
#include "../../include/console.h"
#include "../../include/debug.h"
#include <stdint.h>
#include <stdbool.h>

static bool bootlog_initialized = false;
static bool bootlog_verbose = false;
static uint32_t bootlog_seq = 0;

typedef struct {
    const char* name;
    uint16_t id;
} bootlog_name_id_t;

static const bootlog_name_id_t bootlog_phase_ids[] = {
    {"startup", 1},
    {"kmain", 2},
    {"boot", 3},
    {"cpu", 4},
    {"interrupts", 5},
    {"memory", 6},
    {"apic", 7},
    {"timer", 8},
    {"scheduler", 9},
    {"ipc", 10},
    {"syscall", 11},
    {"security", 12},
    {"loader", 13},
    {"fabric", 14},
    {"vfs", 15},
    {"net", 16},
    {"shell", 17},
    {"threads", 18},
};

static const bootlog_name_id_t bootlog_event_ids[] = {
    {"mark", 1},
    {"enter", 2},
    {"done", 3},
    {"fail", 4},
    {"start", 5},
    {"enable_enter", 6},
    {"enable_done", 7},
    {"kernel_ready", 8},
    {"created", 9},
    {"bootlog_init", 10},
    {"lapic", 11},
    {"pit", 12},
    {"fallback_pic", 13},
    {"kernel_task_fail", 14},
    {"thread_create_fail", 15},
    {"create_enter", 16},
};

static bool bootlog_has_flag(const char* cmdline, const char* needle)
{
    if (!cmdline || !needle) {
        return false;
    }
    return strstr(cmdline, needle) != NULL;
}

static uint16_t bootlog_lookup_id(const bootlog_name_id_t* map, size_t map_len, const char* name)
{
    if (!name) {
        return 0;
    }
    for (size_t i = 0; i < map_len; i++) {
        if (strcmp(map[i].name, name) == 0) {
            return map[i].id;
        }
    }
    return 0;
}

static void bootlog_append_str(char* out, size_t out_len, size_t* pos, const char* s)
{
    if (!out || !pos || !s || *pos >= out_len) {
        return;
    }
    while (*s && *pos + 1 < out_len) {
        out[*pos] = *s;
        (*pos)++;
        s++;
    }
    out[*pos] = '\0';
}

static void bootlog_append_u64(char* out, size_t out_len, size_t* pos, uint64_t v)
{
    char tmp[24];
    size_t n = 0;
    if (v == 0) {
        bootlog_append_str(out, out_len, pos, "0");
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        char c[2];
        c[0] = tmp[--n];
        c[1] = '\0';
        bootlog_append_str(out, out_len, pos, c);
    }
}

static void bootlog_compose_event(char* out, size_t out_len,
                                  const char* phase, const char* event,
                                  uint16_t phase_id, uint16_t event_id,
                                  uint32_t seq, uint32_t cpu, uint64_t ticks)
{
    if (!out || out_len == 0) {
        return;
    }
    if (!phase) {
        phase = "unknown";
    }
    if (!event) {
        event = "mark";
    }
    out[0] = '\0';
    size_t pos = 0;
    bootlog_append_str(out, out_len, &pos, "boot2 seq=");
    bootlog_append_u64(out, out_len, &pos, seq);
    bootlog_append_str(out, out_len, &pos, " ph=");
    bootlog_append_u64(out, out_len, &pos, phase_id);
    bootlog_append_str(out, out_len, &pos, " ev=");
    bootlog_append_u64(out, out_len, &pos, event_id);
    bootlog_append_str(out, out_len, &pos, " cpu=");
    bootlog_append_u64(out, out_len, &pos, cpu);
    bootlog_append_str(out, out_len, &pos, " tk=");
    bootlog_append_u64(out, out_len, &pos, ticks);
    bootlog_append_str(out, out_len, &pos, " p=");
    bootlog_append_str(out, out_len, &pos, phase);
    bootlog_append_str(out, out_len, &pos, " e=");
    bootlog_append_str(out, out_len, &pos, event);
}

void bootlog_init(void)
{
    boot_info_t* info = boot_get_info();
    bootlog_verbose = false;

    if (info) {
        const char* cmdline = info->cmdline;
        if (bootlog_has_flag(cmdline, "startup_debug=0") ||
            bootlog_has_flag(cmdline, "bootlog=quiet")) {
            bootlog_verbose = false;
        }
        if (bootlog_has_flag(cmdline, "startup_debug=1") ||
            bootlog_has_flag(cmdline, "startup_debug=verbose") ||
            bootlog_has_flag(cmdline, "bootlog=verbose")) {
            bootlog_verbose = true;
        }
    }

    bootlog_initialized = true;
    bootlog_mark("startup", "bootlog_init");
}

void bootlog_mark(const char* phase, const char* event)
{
    char tag[80];
    uint16_t phase_id = bootlog_lookup_id(bootlog_phase_ids, ARRAY_SIZE(bootlog_phase_ids), phase);
    uint16_t event_id = bootlog_lookup_id(bootlog_event_ids, ARRAY_SIZE(bootlog_event_ids), event);
    uint32_t cpu = cpu_get_id();
    uint64_t ticks = scheduler_get_ticks();

    if (phase_id == 0) {
        phase_id = 0xFFFF;
    }
    if (event_id == 0) {
        event_id = 0xFFFF;
    }

    bootlog_compose_event(tag, sizeof(tag), phase, event, phase_id, event_id, bootlog_seq, cpu, ticks);
    debug_event(tag);

    if (bootlog_initialized && bootlog_verbose) {
        kprintf("[BOOT2] seq=%llu ph=%u ev=%u cpu=%u tk=%llu p=%s e=%s\n",
                (unsigned long long)bootlog_seq,
                (unsigned)phase_id,
                (unsigned)event_id,
                (unsigned)cpu,
                (unsigned long long)ticks,
                phase ? phase : "unknown",
                event ? event : "mark");
    }
    bootlog_seq++;
}

int bootlog_is_verbose(void)
{
    return bootlog_verbose ? 1 : 0;
}
