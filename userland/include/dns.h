/*
 * dns.h — minimal DNS A-record resolver for RodNIX userland.
 *
 * Sends a recursive UDP query to QEMU SLIRP DNS (10.0.2.3:53) and
 * returns the first IPv4 address found.
 *
 * Addresses are in the RodNIX host-byte-order convention:
 *   MSB = first octet, e.g. 10.0.2.15 → 0x0A00020F
 */

#ifndef _RODNIX_DNS_H
#define _RODNIX_DNS_H

#include <stdint.h>

/*
 * Resolve a hostname to an IPv4 address.
 *
 *   hostname  — DNS name ("example.com") or dotted-decimal ("1.2.3.4").
 *               NULL or empty returns -1.
 *   out_ip    — receives the resolved address in host byte order.
 *
 * Returns 0 on success, -1 on failure (NXDOMAIN, timeout, parse error).
 */
int dns_resolve(const char* hostname, uint32_t* out_ip);

/*
 * Format an IPv4 host-order address into dotted-decimal (buf must be ≥16).
 */
static inline void dns_ip_to_str(uint32_t ip, char* buf)
{
    /* manually itoa each octet to stay dependency-free */
    unsigned a = (ip >> 24) & 0xFFu;
    unsigned b = (ip >> 16) & 0xFFu;
    unsigned c = (ip >>  8) & 0xFFu;
    unsigned d =  ip        & 0xFFu;
    /* write "a.b.c.d\0" */
    int pos = 0;
#define _OCTET(v) \
    do { \
        if ((v) >= 100) { buf[pos++] = (char)('0' + (v) / 100); } \
        if ((v) >=  10) { buf[pos++] = (char)('0' + ((v) % 100) / 10); } \
        buf[pos++] = (char)('0' + (v) % 10); \
    } while (0)
    _OCTET(a); buf[pos++] = '.';
    _OCTET(b); buf[pos++] = '.';
    _OCTET(c); buf[pos++] = '.';
    _OCTET(d);
#undef _OCTET
    buf[pos] = '\0';
}

#endif /* _RODNIX_DNS_H */
