/**
 * @file tracev2.c
 * @brief Structured runtime trace events (v2).
 */

#include "tracev2.h"
#include "bootlog.h"
#include "../core/cpu.h"
#include "scheduler.h"
#include "../../include/common.h"
#include "../../include/console.h"
#include "../../include/debug.h"

static uint32_t tracev2_seq = 0;

static void tr2_append_str(char* out, size_t out_len, size_t* pos, const char* s)
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

static void tr2_append_u64(char* out, size_t out_len, size_t* pos, uint64_t v)
{
    char tmp[24];
    size_t n = 0;
    if (v == 0) {
        tr2_append_str(out, out_len, pos, "0");
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
        tr2_append_str(out, out_len, pos, c);
    }
}

void tracev2_emit(uint16_t cat, uint16_t ev, uint64_t a0, uint64_t a1)
{
    uint32_t seq = tracev2_seq++;
    uint32_t cpu = cpu_get_id();
    uint64_t tk = scheduler_get_ticks();

    char ring_line[80];
    size_t pos = 0;
    ring_line[0] = '\0';
    tr2_append_str(ring_line, sizeof(ring_line), &pos, "tr2 s=");
    tr2_append_u64(ring_line, sizeof(ring_line), &pos, seq);
    tr2_append_str(ring_line, sizeof(ring_line), &pos, " c=");
    tr2_append_u64(ring_line, sizeof(ring_line), &pos, cat);
    tr2_append_str(ring_line, sizeof(ring_line), &pos, " e=");
    tr2_append_u64(ring_line, sizeof(ring_line), &pos, ev);
    tr2_append_str(ring_line, sizeof(ring_line), &pos, " cpu=");
    tr2_append_u64(ring_line, sizeof(ring_line), &pos, cpu);
    tr2_append_str(ring_line, sizeof(ring_line), &pos, " tk=");
    tr2_append_u64(ring_line, sizeof(ring_line), &pos, tk);
    tr2_append_str(ring_line, sizeof(ring_line), &pos, " a0=");
    tr2_append_u64(ring_line, sizeof(ring_line), &pos, a0);
    tr2_append_str(ring_line, sizeof(ring_line), &pos, " a1=");
    tr2_append_u64(ring_line, sizeof(ring_line), &pos, a1);

    debug_event(ring_line);

    if (bootlog_is_verbose()) {
        kprintf("[TR2] s=%llu c=%u e=%u cpu=%u tk=%llu a0=%llu a1=%llu\n",
                (unsigned long long)seq,
                (unsigned)cat,
                (unsigned)ev,
                (unsigned)cpu,
                (unsigned long long)tk,
                (unsigned long long)a0,
                (unsigned long long)a1);
    }
}
