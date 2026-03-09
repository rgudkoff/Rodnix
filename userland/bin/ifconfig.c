/*
 * ifconfig.c
 * Minimal interface list utility for Rodnix.
 */

#include <stdint.h>
#include "unistd.h"
#include "posix_syscall.h"
#include "netif.h"
#include <arpa/inet.h>
#include <netinet/in.h>

#define FD_STDOUT 1
#define NETIF_MAX_QUERY 16

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

static void write_hex_byte(uint8_t b)
{
    static const char h[] = "0123456789ABCDEF";
    char o[2];
    o[0] = h[(b >> 4) & 0x0F];
    o[1] = h[b & 0x0F];
    (void)write_buf(o, 2);
}

static void write_mac(const uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        write_hex_byte(mac[i]);
        if (i != 5) {
            (void)write_buf(":", 1);
        }
    }
}

static void write_ipv4(uint32_t ip_host_order)
{
    char buf[18];
    struct in_addr addr;
    addr.s_addr = htonl(ip_host_order);
    if (inet_ntop(AF_INET, &addr, buf, (socklen_t)sizeof(buf)) != NULL) {
        (void)write_str(buf);
        return;
    }

    /* Defensive fallback keeps utility usable if libc inet path fails. */
    write_u64((uint64_t)((ip_host_order >> 24) & 0xFFu));
    (void)write_buf(".", 1);
    write_u64((uint64_t)((ip_host_order >> 16) & 0xFFu));
    (void)write_buf(".", 1);
    write_u64((uint64_t)((ip_host_order >> 8) & 0xFFu));
    (void)write_buf(".", 1);
    write_u64((uint64_t)(ip_host_order & 0xFFu));
}

int main(void)
{
    netif_info_t ifs[NETIF_MAX_QUERY];
    uint32_t total = 0;
    long n = posix_netiflist(ifs, NETIF_MAX_QUERY, &total);
    if (n < 0) {
        (void)write_str("ifconfig: netiflist failed\n");
        return 1;
    }

    (void)write_str("ifconfig: interfaces=");
    write_u64((uint64_t)total);
    (void)write_str("\n");

    for (long i = 0; i < n; i++) {
        (void)write_str(ifs[i].name);
        (void)write_str(": mtu ");
        write_u64((uint64_t)ifs[i].mtu);
        (void)write_str(" flags=0x");
        write_hex_byte((uint8_t)((ifs[i].flags >> 8) & 0xFF));
        write_hex_byte((uint8_t)(ifs[i].flags & 0xFF));
        (void)write_str("\n  ether ");
        write_mac(ifs[i].mac);
        if (ifs[i].ipv4_addr != 0) {
            (void)write_str("\n  inet ");
            write_ipv4(ifs[i].ipv4_addr);
            (void)write_str(" netmask ");
            write_ipv4(ifs[i].ipv4_netmask);
            if (ifs[i].ipv4_gateway != 0) {
                (void)write_str(" gateway ");
                write_ipv4(ifs[i].ipv4_gateway);
            }
        }
        (void)write_str("\n  rx=");
        write_u64(ifs[i].stats.rx_frames);
        (void)write_str(" tx=");
        write_u64(ifs[i].stats.tx_frames);
        (void)write_str(" drops=");
        write_u64(ifs[i].stats.drops);
        (void)write_str("\n");
    }

    if (total > (uint32_t)n) {
        (void)write_str("ifconfig: output truncated\n");
    }
    return 0;
}
