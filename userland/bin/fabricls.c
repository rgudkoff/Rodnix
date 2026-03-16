/*
 * fabricls.c
 * List Fabric node topology.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "fabric_node.h"

#define FD_STDOUT 1
#define FABRICLS_MAX_QUERY 256

/* Keep result buffer out of user stack (stack is intentionally small). */
static fabric_node_info_t g_nodes[FABRICLS_MAX_QUERY];

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

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

static const char* state_name(uint32_t st)
{
    switch (st) {
        case 1: return "init";
        case 2: return "discovered";
        case 3: return "attached";
        case 4: return "published";
        case 5: return "registered";
        case 6: return "active";
        case 7: return "error";
        case 8: return "removed";
        default: return "unspec";
    }
}

int main(void)
{
    uint32_t total = 0;
    long n = posix_fabricls(g_nodes, FABRICLS_MAX_QUERY, &total);
    if (n < 0) {
        (void)write_str("fabricls: syscall failed\n");
        return 1;
    }

    (void)write_str("fabric nodes: ");
    write_u64((uint64_t)total);
    (void)write_str("\n");

    for (long i = 0; i < n; i++) {
        (void)write_str(g_nodes[i].path);
        if (g_nodes[i].name[0]) {
            (void)write_str("  ");
            (void)write_str(g_nodes[i].name);
        }
        if (g_nodes[i].driver[0]) {
            (void)write_str(" [driver=");
            (void)write_str(g_nodes[i].driver);
            (void)write_str("]");
        }
        if (g_nodes[i].provider_path[0]) {
            (void)write_str(" -> ");
            (void)write_str(g_nodes[i].provider_path);
        }
        (void)write_str(" {state=");
        (void)write_str(state_name(g_nodes[i].state));
        (void)write_str("}");
        (void)write_str("\n");
    }

    if (total > (uint32_t)n) {
        (void)write_str("fabricls: output truncated\n");
    }
    return 0;
}
