/*
 * ping.c
 * Minimal ICMP echo utility via POSIX_SYS_PING.
 */

#include <stdint.h>
#include "posix_syscall.h"

#define FD_STDOUT 1

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
    for (int l = 0, r = i - 1; l < r; l++, r--) {
        char t = buf[l];
        buf[l] = buf[r];
        buf[r] = t;
    }
    (void)write_buf(buf, (uint64_t)i);
}

int main(int argc, char** argv)
{
    const char* ip_text = "10.0.2.2";
    uint32_t timeout_ms = 1000;

    if (argc >= 2 && argv[1] && argv[1][0]) {
        ip_text = argv[1];
    }
    if (argc >= 3 && argv[2] && argv[2][0]) {
        uint32_t t = 0;
        for (int i = 0; argv[2][i]; i++) {
            char c = argv[2][i];
            if (c < '0' || c > '9') {
                (void)write_str("ping: invalid timeout\n");
                return 1;
            }
            t = t * 10u + (uint32_t)(c - '0');
        }
        if (t > 0) {
            timeout_ms = t;
        }
    }

    uint32_t oct[4] = {0, 0, 0, 0};
    uint32_t cur = 0;
    uint32_t part = 0;
    for (int i = 0;; i++) {
        char c = ip_text[i];
        if (c >= '0' && c <= '9') {
            cur = cur * 10u + (uint32_t)(c - '0');
            if (cur > 255u) {
                (void)write_str("ping: invalid ipv4 address\n");
                return 1;
            }
            continue;
        }
        if (c == '.' || c == '\0') {
            if (part >= 4u) {
                (void)write_str("ping: invalid ipv4 address\n");
                return 1;
            }
            oct[part++] = cur;
            cur = 0;
            if (c == '\0') {
                break;
            }
            continue;
        }
        (void)write_str("ping: invalid ipv4 address\n");
        return 1;
    }
    if (part != 4u) {
        (void)write_str("ping: invalid ipv4 address\n");
        return 1;
    }

    uint32_t rtt_ms = 0;
    uint32_t dst_host = (oct[0] << 24) | (oct[1] << 16) | (oct[2] << 8) | oct[3];
    long rc = posix_ping(dst_host, timeout_ms, &rtt_ms);
    if (rc < 0) {
        (void)write_str("ping: timeout\n");
        return 1;
    }

    (void)write_str("ping: reply from ");
    (void)write_str(ip_text);
    (void)write_str(" time=");
    write_u64((uint64_t)rtt_ms);
    (void)write_str(" ms\n");
    return 0;
}
