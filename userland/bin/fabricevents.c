/*
 * fabricevents.c
 * Drain and print Fabric events.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "fabric_event.h"

#define FD_STDOUT 1
#define FABRICEVENTS_MAX 64

static fabric_event_t g_events[FABRICEVENTS_MAX];

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

static const char* event_name(uint32_t type)
{
    switch (type) {
        case FABRIC_EVENT_DEVICE_ADDED: return "device_added";
        case FABRIC_EVENT_DRIVER_ATTACHED: return "driver_attached";
        case FABRIC_EVENT_SERVICE_PUBLISHED: return "service_published";
        case FABRIC_EVENT_SERVICE_REGISTERED: return "service_registered";
        case FABRIC_EVENT_STATE_CHANGED: return "state_changed";
        default: return "unknown";
    }
}

int main(void)
{
    uint32_t read = 0;
    uint32_t dropped = 0;
    long n = posix_fabricevents(g_events, FABRICEVENTS_MAX, &read, &dropped);
    if (n < 0) {
        (void)write_str("fabricevents: syscall failed\n");
        return 1;
    }

    (void)write_str("fabric events: ");
    write_u64((uint64_t)read);
    (void)write_str("\n");

    for (uint32_t i = 0; i < read; i++) {
        (void)write_str("#");
        write_u64(g_events[i].seq);
        (void)write_str(" ");
        (void)write_str(event_name(g_events[i].type));
        (void)write_str(" ");
        (void)write_str(g_events[i].node_path);
        if (g_events[i].subject[0]) {
            (void)write_str(" subject=");
            (void)write_str(g_events[i].subject);
        }
        if (g_events[i].detail[0]) {
            (void)write_str(" detail=");
            (void)write_str(g_events[i].detail);
        }
        (void)write_str("\n");
    }

    if (dropped > 0) {
        (void)write_str("fabricevents: dropped=");
        write_u64((uint64_t)dropped);
        (void)write_str("\n");
    }
    return 0;
}
