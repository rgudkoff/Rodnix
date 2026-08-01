/*
 * dns.c — minimal recursive DNS A-record resolver for RodNIX userland.
 *
 * Sends a single UDP query to 10.0.2.3:53 (QEMU SLIRP resolver) and
 * parses the first A-record answer.  Dotted-decimal addresses bypass
 * the network entirely.
 *
 * All multi-byte DNS fields are big-endian.  RodNIX stores IPv4
 * addresses in host order where MSB = first octet (10.0.2.15 = 0x0A00020F),
 * which is the same as reading the four A-record RDATA bytes MSB-first.
 */

#include <stdint.h>
#include <string.h>
#include "posix_syscall.h"
#include "dns.h"

/* QEMU SLIRP DNS server: 10.0.2.3:53 */
#define DNS_SRV_IP    0x0A000203u
#define DNS_SRV_PORT  53u
/* Ephemeral local port range for DNS client sockets */
#define DNS_CLI_PORT_BASE  10100u
#define DNS_CLI_PORT_RANGE 100u

#define DNS_TIMEOUT_MS 4000u
#define DNS_TYPE_A     1u
#define DNS_CLASS_IN   1u

/* Kernel-compatible sockaddr_in (matches net/socket.h, NOT netinet/in.h) */
typedef struct {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
} dns_sa_t;

#define DNS_AF_INET   2
#define DNS_SOCK_DGRAM 2

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

static uint16_t dns_r16(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/*
 * Encode a dotted hostname as DNS labels into buf[0..bufsz).
 * Returns byte count written, or -1 on error.
 */
static int dns_encode_name(uint8_t* buf, int bufsz, const char* host)
{
    int pos = 0;
    const char* p = host;

    while (*p) {
        /* find end of this label */
        const char* dot = p;
        while (*dot && *dot != '.') {
            dot++;
        }
        int label = (int)(dot - p);
        if (label == 0 || label > 63) {
            return -1;
        }
        if (pos + 1 + label >= bufsz) {
            return -1;
        }
        buf[pos++] = (uint8_t)label;
        memcpy(buf + pos, p, (size_t)label);
        pos += label;
        p = (*dot == '.') ? dot + 1 : dot;
    }
    if (pos + 1 >= bufsz) {
        return -1;
    }
    buf[pos++] = 0; /* root label */
    return pos;
}

/*
 * Skip a DNS name field in a response (handles compression pointers).
 * Returns the offset just past the name, or -1 on error.
 */
static int dns_skip_name(const uint8_t* pkt, int pktlen, int off)
{
    int iters = 0;
    while (off < pktlen) {
        if (iters++ > 64) {
            return -1; /* loop guard */
        }
        uint8_t b = pkt[off];
        if (b == 0) {
            return off + 1; /* end-of-name */
        }
        if ((b & 0xC0u) == 0xC0u) {
            /* 2-byte compression pointer — name ends here */
            if (off + 2 > pktlen) {
                return -1;
            }
            return off + 2;
        }
        if ((b & 0xC0u) != 0) {
            return -1; /* reserved bits */
        }
        off += 1 + (int)b; /* skip label */
    }
    return -1;
}

/*
 * Try to parse "a.b.c.d" as a literal IPv4 address.
 * Returns 0 and sets *ip on success, -1 if not a literal.
 */
static int dns_parse_literal(const char* s, uint32_t* ip)
{
    uint32_t octs[4];
    int idx = 0;
    uint32_t acc = 0;
    int digits = 0;

    for (const char* p = s; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            acc = acc * 10u + (uint32_t)(c - '0');
            if (acc > 255u) {
                return -1;
            }
            digits++;
        } else if (c == '.' || c == '\0') {
            if (digits == 0 || idx >= 4) {
                return -1;
            }
            octs[idx++] = acc;
            acc = 0;
            digits = 0;
            if (c == '\0') {
                break;
            }
        } else {
            return -1; /* non-numeric character */
        }
    }
    if (idx != 4) {
        return -1;
    }
    *ip = (octs[0] << 24) | (octs[1] << 16) | (octs[2] << 8) | octs[3];
    return 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

int dns_resolve(const char* hostname, uint32_t* out_ip)
{
    if (!hostname || !out_ip || hostname[0] == '\0') {
        return -1;
    }

    /* Fast path: literal dotted-decimal address */
    if (dns_parse_literal(hostname, out_ip) == 0) {
        return 0;
    }

    /* Pick a local client port (rotates to avoid immediate re-use) */
    static uint16_t s_port = DNS_CLI_PORT_BASE;
    uint16_t cli_port = s_port++;
    if (s_port >= DNS_CLI_PORT_BASE + DNS_CLI_PORT_RANGE) {
        s_port = DNS_CLI_PORT_BASE;
    }

    /* ---- Build query packet ------------------------------------------ */
    uint8_t qpkt[512];
    int qlen = 0;
    memset(qpkt, 0, sizeof(qpkt));

    /* Header */
    qpkt[qlen++] = 0x12; qpkt[qlen++] = 0x34; /* ID = 0x1234 */
    qpkt[qlen++] = 0x01; qpkt[qlen++] = 0x00; /* Flags: RD=1 */
    qpkt[qlen++] = 0x00; qpkt[qlen++] = 0x01; /* QDCOUNT = 1 */
    qpkt[qlen++] = 0x00; qpkt[qlen++] = 0x00; /* ANCOUNT = 0 */
    qpkt[qlen++] = 0x00; qpkt[qlen++] = 0x00; /* NSCOUNT = 0 */
    qpkt[qlen++] = 0x00; qpkt[qlen++] = 0x00; /* ARCOUNT = 0 */

    /* QNAME */
    int nlen = dns_encode_name(qpkt + qlen,
                               (int)sizeof(qpkt) - qlen - 4,
                               hostname);
    if (nlen < 0) {
        return -1;
    }
    qlen += nlen;

    /* QTYPE = A (1) */
    qpkt[qlen++] = 0x00; qpkt[qlen++] = 0x01;
    /* QCLASS = IN (1) */
    qpkt[qlen++] = 0x00; qpkt[qlen++] = 0x01;

    /* ---- Send query -------------------------------------------------- */
    long fd = posix_socket(DNS_AF_INET, DNS_SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    dns_sa_t local = {0};
    local.sin_family = DNS_AF_INET;
    local.sin_port   = cli_port;
    local.sin_addr   = 0; /* INADDR_ANY */
    if (posix_bind((int)fd, &local) < 0) {
        posix_close((int)fd);
        return -1;
    }

    dns_sa_t srv = {0};
    srv.sin_family = DNS_AF_INET;
    srv.sin_port   = DNS_SRV_PORT;
    srv.sin_addr   = DNS_SRV_IP;
    long sent = posix_sendto((int)fd, qpkt, (uint64_t)qlen, 0,
                             &srv, sizeof(srv));
    if (sent != (long)qlen) {
        posix_close((int)fd);
        return -1;
    }

    /* ---- Receive response -------------------------------------------- */
    uint8_t rpkt[512];
    long rlen = posix_recvfrom((int)fd, rpkt, sizeof(rpkt), 0,
                               NULL, DNS_TIMEOUT_MS);
    posix_close((int)fd);

    if (rlen < 12) {
        return -1; /* too short */
    }

    /* ---- Parse header ------------------------------------------------ */
    uint16_t flags   = dns_r16(rpkt + 2);
    uint16_t ancount = dns_r16(rpkt + 6);

    if (!(flags & 0x8000u)) {
        return -1; /* QR bit not set — not a response */
    }
    if ((flags & 0x000Fu) != 0) {
        return -1; /* RCODE != NOERROR */
    }
    if (ancount == 0) {
        return -1; /* no answers */
    }

    /* ---- Skip question section --------------------------------------- */
    int off = 12;
    off = dns_skip_name(rpkt, (int)rlen, off);
    if (off < 0) {
        return -1;
    }
    off += 4; /* skip QTYPE + QCLASS */
    if (off > (int)rlen) {
        return -1;
    }

    /* ---- Parse answer records ---------------------------------------- */
    int limit = (int)ancount;
    if (limit > 16) {
        limit = 16;
    }
    for (int i = 0; i < limit; i++) {
        if (off >= (int)rlen) {
            break;
        }
        /* Skip NAME (label or compression pointer) */
        off = dns_skip_name(rpkt, (int)rlen, off);
        if (off < 0) {
            break;
        }
        if (off + 10 > (int)rlen) {
            break;
        }
        uint16_t rtype  = dns_r16(rpkt + off);
        /* skip CLASS (2) + TTL (4) */
        uint16_t rdlen  = dns_r16(rpkt + off + 8);
        off += 10;

        if (rtype == DNS_TYPE_A && rdlen == 4) {
            if (off + 4 > (int)rlen) {
                break;
            }
            /* IPv4: read 4 bytes MSB-first → host-order (0x0A00020F = 10.0.2.15) */
            *out_ip = ((uint32_t)rpkt[off]     << 24) |
                      ((uint32_t)rpkt[off + 1] << 16) |
                      ((uint32_t)rpkt[off + 2] <<  8) |
                       (uint32_t)rpkt[off + 3];
            return 0;
        }
        off += (int)rdlen; /* skip non-A record */
    }

    return -1; /* no A record found */
}
