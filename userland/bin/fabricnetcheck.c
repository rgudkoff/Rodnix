/*
 * fabricnetcheck.c
 * Validate Fabric network lifecycle ordering.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "fabric_event.h"
#include "fabric_node.h"

#define FD_STDOUT 1
#define MAX_EVENTS 128
#define MAX_NET_SVC 16

typedef struct svc_state {
    char path[FABRIC_NODE_PATH_MAX];
    uint64_t published_seq;
    uint64_t registered_seq;
    uint64_t active_seq;
} svc_state_t;

static fabric_event_t g_events[MAX_EVENTS];
static svc_state_t g_svcs[MAX_NET_SVC];
static fabric_node_info_t g_nodes[256];

static long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static int starts_with(const char* s, const char* p)
{
    while (*p) {
        if (*s != *p) {
            return 0;
        }
        s++;
        p++;
    }
    return 1;
}

static int str_eq(const char* a, const char* b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

static int str_contains(const char* s, const char* needle)
{
    if (!s || !needle || !needle[0]) {
        return 0;
    }
    while (*s) {
        const char* a = s;
        const char* b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b) {
            return 1;
        }
        s++;
    }
    return 0;
}

static void str_copy(char* dst, uint64_t cap, const char* src)
{
    if (!dst || cap == 0) {
        return;
    }
    uint64_t i = 0;
    while (src && src[i] && (i + 1) < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int is_net_svc_name(const char* name)
{
    if (!name || !name[0]) {
        return 0;
    }
    return starts_with(name, "net") || starts_with(name, "eth");
}

static int find_or_add_svc(const char* path)
{
    for (int i = 0; i < MAX_NET_SVC; i++) {
        if (g_svcs[i].path[0] && str_eq(g_svcs[i].path, path)) {
            return i;
        }
    }
    for (int i = 0; i < MAX_NET_SVC; i++) {
        if (!g_svcs[i].path[0]) {
            str_copy(g_svcs[i].path, sizeof(g_svcs[i].path), path);
            return i;
        }
    }
    return -1;
}

int main(void)
{
    uint32_t read = 0;
    uint32_t dropped = 0;
    long n = posix_fabricevents(g_events, MAX_EVENTS, &read, &dropped);
    if (n < 0) {
        (void)write_str("fabricnetcheck: events syscall failed\n");
        return 1;
    }

    uint64_t min_net_attach = 0;
    for (uint32_t i = 0; i < read; i++) {
        fabric_event_t* e = &g_events[i];
        if (e->type == FABRIC_EVENT_DRIVER_ATTACHED && str_contains(e->detail, "net")) {
            if (min_net_attach == 0 || e->seq < min_net_attach) {
                min_net_attach = e->seq;
            }
        }
        if (!starts_with(e->node_path, "/fabric/services/")) {
            continue;
        }
        if (!is_net_svc_name(e->subject)) {
            continue;
        }
        int idx = find_or_add_svc(e->node_path);
        if (idx < 0) {
            continue;
        }
        if (e->type == FABRIC_EVENT_SERVICE_PUBLISHED && g_svcs[idx].published_seq == 0) {
            g_svcs[idx].published_seq = e->seq;
        } else if (e->type == FABRIC_EVENT_SERVICE_REGISTERED && g_svcs[idx].registered_seq == 0) {
            g_svcs[idx].registered_seq = e->seq;
        } else if (e->type == FABRIC_EVENT_STATE_CHANGED && str_eq(e->detail, "active")) {
            g_svcs[idx].active_seq = e->seq;
        }
    }

    int checked = 0;
    for (int i = 0; i < MAX_NET_SVC; i++) {
        if (!g_svcs[i].path[0]) {
            continue;
        }
        checked++;
        if (g_svcs[i].published_seq == 0 || g_svcs[i].registered_seq == 0) {
            (void)write_str("fabricnetcheck: missing publish/register for ");
            (void)write_str(g_svcs[i].path);
            (void)write_str("\n");
            return 2;
        }
        if (g_svcs[i].published_seq > g_svcs[i].registered_seq) {
            (void)write_str("fabricnetcheck: wrong order publish>register for ");
            (void)write_str(g_svcs[i].path);
            (void)write_str("\n");
            return 3;
        }
        if (g_svcs[i].active_seq && g_svcs[i].active_seq < g_svcs[i].registered_seq) {
            (void)write_str("fabricnetcheck: active before registered for ");
            (void)write_str(g_svcs[i].path);
            (void)write_str("\n");
            return 4;
        }
        if (min_net_attach && g_svcs[i].published_seq < min_net_attach) {
            (void)write_str("fabricnetcheck: service published before net attach for ");
            (void)write_str(g_svcs[i].path);
            (void)write_str("\n");
            return 5;
        }
    }

    if (checked == 0) {
        uint32_t total = 0;
        long nn = posix_fabricls(g_nodes, 256, &total);
        if (nn > 0) {
            int snap_seen = 0;
            for (long i = 0; i < nn; i++) {
                if (!starts_with(g_nodes[i].path, "/fabric/services/")) {
                    continue;
                }
                if (!is_net_svc_name(g_nodes[i].name)) {
                    continue;
                }
                snap_seen = 1;
                if (g_nodes[i].state < 5) { /* registered */
                    (void)write_str("fabricnetcheck: snapshot net service not registered: ");
                    (void)write_str(g_nodes[i].path);
                    (void)write_str("\n");
                    return 7;
                }
            }
            if (snap_seen) {
                (void)write_str("fabricnetcheck: OK (snapshot fallback)\n");
                return 0;
            }
        }
        (void)write_str("fabricnetcheck: no net services yet\n");
        return 0;
    }
    if (dropped > 0) {
        (void)write_str("fabricnetcheck: warning events dropped\n");
    }
    (void)write_str("fabricnetcheck: OK\n");
    return 0;
}
