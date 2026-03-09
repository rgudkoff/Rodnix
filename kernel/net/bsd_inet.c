#include "bsd_inet.h"
#include "../../include/common.h"

static uint16_t bswap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static uint32_t bswap32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}

uint16_t bsd_htons(uint16_t v)
{
    return bswap16(v);
}

uint16_t bsd_ntohs(uint16_t v)
{
    return bswap16(v);
}

uint32_t bsd_htonl(uint32_t v)
{
    return bswap32(v);
}

uint32_t bsd_ntohl(uint32_t v)
{
    return bswap32(v);
}

/*
 * Internet checksum over a contiguous memory block.
 * Behavior mirrors the standard BSD in_cksum reduction.
 */
uint16_t bsd_in_cksum(const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len > 0) {
        sum += (uint16_t)((uint16_t)p[0] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

uint16_t bsd_icmp_checksum(const void* data, size_t len)
{
    return bsd_in_cksum(data, len);
}

/*
 * BSD-style IPv4 pseudo-header accumulation helper.
 * proto_be_len = htons((proto << 8) + udp_len).
 */
static uint32_t cksum_add_buf(uint32_t sum, const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    while (len > 1) {
        sum += (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len > 0) {
        sum += (uint16_t)((uint16_t)p[0] << 8);
    }
    return sum;
}

static uint16_t cksum_finalize(uint32_t sum)
{
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

uint16_t bsd_in_pseudo(uint32_t src_host, uint32_t dst_host, uint16_t proto_len_host)
{
    uint8_t ph[12];
    uint32_t src_be = bsd_htonl(src_host);
    uint32_t dst_be = bsd_htonl(dst_host);
    uint16_t len_be = bsd_htons(proto_len_host);

    ph[0] = (uint8_t)((src_be >> 24) & 0xFFu);
    ph[1] = (uint8_t)((src_be >> 16) & 0xFFu);
    ph[2] = (uint8_t)((src_be >> 8) & 0xFFu);
    ph[3] = (uint8_t)(src_be & 0xFFu);
    ph[4] = (uint8_t)((dst_be >> 24) & 0xFFu);
    ph[5] = (uint8_t)((dst_be >> 16) & 0xFFu);
    ph[6] = (uint8_t)((dst_be >> 8) & 0xFFu);
    ph[7] = (uint8_t)(dst_be & 0xFFu);
    ph[8] = 0;
    ph[9] = (uint8_t)BSD_IPPROTO_UDP;
    ph[10] = (uint8_t)((len_be >> 8) & 0xFFu);
    ph[11] = (uint8_t)(len_be & 0xFFu);

    return (uint16_t)(~cksum_add_buf(0, ph, sizeof(ph)) & 0xFFFFu);
}

uint16_t bsd_udp4_checksum(uint32_t src_host,
                           uint32_t dst_host,
                           const bsd_udphdr_t* uh,
                           const void* payload,
                           size_t payload_len)
{
    if (!uh) {
        return 0;
    }

    uint8_t ph[12];
    uint32_t src_be = bsd_htonl(src_host);
    uint32_t dst_be = bsd_htonl(dst_host);
    uint16_t udp_len = bsd_ntohs(uh->uh_ulen);
    uint16_t udp_len_be = bsd_htons(udp_len);
    bsd_udphdr_t uh_local;

    ph[0] = (uint8_t)((src_be >> 24) & 0xFFu);
    ph[1] = (uint8_t)((src_be >> 16) & 0xFFu);
    ph[2] = (uint8_t)((src_be >> 8) & 0xFFu);
    ph[3] = (uint8_t)(src_be & 0xFFu);
    ph[4] = (uint8_t)((dst_be >> 24) & 0xFFu);
    ph[5] = (uint8_t)((dst_be >> 16) & 0xFFu);
    ph[6] = (uint8_t)((dst_be >> 8) & 0xFFu);
    ph[7] = (uint8_t)(dst_be & 0xFFu);
    ph[8] = 0;
    ph[9] = (uint8_t)BSD_IPPROTO_UDP;
    ph[10] = (uint8_t)((udp_len_be >> 8) & 0xFFu);
    ph[11] = (uint8_t)(udp_len_be & 0xFFu);

    memcpy(&uh_local, uh, sizeof(uh_local));
    uh_local.uh_sum = 0;

    uint32_t sum = 0;
    sum = cksum_add_buf(sum, ph, sizeof(ph));
    sum = cksum_add_buf(sum, &uh_local, sizeof(uh_local));
    sum = cksum_add_buf(sum, payload, payload_len);

    uint16_t out = cksum_finalize(sum);
    return (out == 0) ? 0xFFFFu : out;
}
