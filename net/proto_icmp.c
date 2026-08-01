/*
 * proto_icmp.c — ICMP echo handling and the ping slot.
 *
 * Echo requests are answered straight from the input path. Echo replies are
 * matched against a single outstanding request; the slot is armed by
 * icmp_proto_send_echo() and drained by icmp_proto_poll_reply(). All waiting
 * happens in the socket layer — nothing here blocks.
 */

#include "proto_icmp.h"
#include "net_proto.h"
#include "socket.h"
#include "bsd_inet.h"
#include "bsd_ether.h"
#include "../kernel/fabric/service/net_service.h"
#include "../kernel/fabric/spin.h"
#include "../lib/heap.h"
#include "../sched/scheduler.h"
#include "../include/common.h"
#include "../include/error.h"

#define NET_PING_ID 0x524Eu

typedef struct ping_state {
    spinlock_t lock;
    uint16_t seq;
    uint16_t await_seq;
    uint32_t await_dst;
    uint64_t start_tick;
    uint32_t last_rtt_ms;
    int waiting;
    int received;
} ping_state_t;

static ping_state_t g_ping;

void icmp_proto_init(void)
{
    spinlock_init(&g_ping.lock);
    g_ping.seq = 0;
    g_ping.await_seq = 0;
    g_ping.await_dst = 0;
    g_ping.start_tick = 0;
    g_ping.last_rtt_ms = 0;
    g_ping.waiting = 0;
    g_ping.received = 0;
}

int icmp_proto_send_echo_reply(fabric_netif_t* iface,
                               const bsd_ether_header_t* rx_eh,
                               const bsd_ip_t* rx_ip,
                               const bsd_icmp_echo_t* rx_icmp,
                               size_t icmp_len)
{
    if (!iface || !rx_eh || !rx_ip || !rx_icmp || icmp_len < sizeof(bsd_icmp_echo_t)) {
        return -1;
    }

    const size_t eth_len = sizeof(bsd_ether_header_t);
    const size_t ip_len = sizeof(bsd_ip_t);
    const size_t frame_len = eth_len + ip_len + icmp_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return -1;
    }
    memset(frame, 0, frame_len);

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_dhost, rx_eh->ether_shost, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_shost, iface->mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + eth_len);
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(ip_len + icmp_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = BSD_IP_TTL_DEF;
    ip->ip_p = BSD_IPPROTO_ICMP;
    ip->ip_src = rx_ip->ip_dst;
    ip->ip_dst = rx_ip->ip_src;
    ip->ip_sum = 0;
    ip->ip_sum = bsd_htons(bsd_in_cksum(ip, sizeof(*ip)));

    uint8_t* icmp = frame + eth_len + ip_len;
    memcpy(icmp, rx_icmp, icmp_len);
    bsd_icmp_echo_t* echo = (bsd_icmp_echo_t*)icmp;
    echo->type = BSD_ICMP_ECHOREPLY;
    echo->code = 0;
    echo->cksum = 0;
    echo->cksum = bsd_htons(bsd_icmp_checksum(icmp, icmp_len));

    /* The reply goes back to the request's source, which for a self-directed
     * ping is one of our own addresses — emit so it loops rather than
     * disappearing onto the wire. */
    int rc = net_frame_emit(iface, bsd_ntohl(rx_ip->ip_src),
                            frame, (uint32_t)frame_len);
    kfree(frame);
    return rc;
}

void icmp_proto_note_echo_reply(uint32_t src_ip, uint16_t id, uint16_t seq)
{
    spinlock_lock(&g_ping.lock);
    if (g_ping.waiting && id == NET_PING_ID &&
        seq == g_ping.await_seq && src_ip == g_ping.await_dst) {
        uint64_t now = scheduler_get_ticks();
        uint64_t dt = (now >= g_ping.start_tick) ? (now - g_ping.start_tick) : 0;
        g_ping.last_rtt_ms = (uint32_t)net_ticks_to_ms(dt);
        g_ping.received = 1;
        g_ping.waiting = 0;
    }
    spinlock_unlock(&g_ping.lock);
}

uint16_t icmp_proto_send_echo(fabric_netif_t* iface,
                              uint32_t src_host, uint32_t dst_host,
                              const uint8_t src_mac[BSD_ETHER_ADDR_LEN],
                              const uint8_t dst_mac[BSD_ETHER_ADDR_LEN])
{
    if (!iface || !src_mac || !dst_mac) {
        return 0;
    }

    uint8_t payload[16] = { 'r','o','d','n','i','x','-','p','i','n','g',0,1,2,3,4 };
    const size_t icmp_len = sizeof(bsd_icmp_echo_t) + sizeof(payload);
    const size_t frame_len = sizeof(bsd_ether_header_t) + sizeof(bsd_ip_t) + icmp_len;

    uint8_t* frame = (uint8_t*)kmalloc(frame_len);
    if (!frame) {
        return 0;
    }
    memset(frame, 0, frame_len);

    spinlock_lock(&g_ping.lock);
    uint16_t seq = ++g_ping.seq;
    g_ping.await_seq = seq;
    g_ping.await_dst = dst_host;
    g_ping.start_tick = scheduler_get_ticks();
    g_ping.last_rtt_ms = 0;
    g_ping.waiting = 1;
    g_ping.received = 0;
    spinlock_unlock(&g_ping.lock);

    bsd_ether_header_t* eh = (bsd_ether_header_t*)frame;
    memcpy(eh->ether_shost, src_mac, BSD_ETHER_ADDR_LEN);
    memcpy(eh->ether_dhost, dst_mac, BSD_ETHER_ADDR_LEN);
    eh->ether_type = bsd_htons(BSD_ETHERTYPE_IP);

    bsd_ip_t* ip = (bsd_ip_t*)(frame + sizeof(*eh));
    ip->ip_vhl = (uint8_t)((BSD_IPVERSION << 4) | (sizeof(bsd_ip_t) / 4));
    ip->ip_tos = 0;
    ip->ip_len = bsd_htons((uint16_t)(sizeof(bsd_ip_t) + icmp_len));
    ip->ip_id = 0;
    ip->ip_off = bsd_htons(BSD_IP_DF);
    ip->ip_ttl = BSD_IP_TTL_DEF;
    ip->ip_p = BSD_IPPROTO_ICMP;
    ip->ip_src = bsd_htonl(src_host);
    ip->ip_dst = bsd_htonl(dst_host);
    ip->ip_sum = 0;
    ip->ip_sum = bsd_htons(bsd_in_cksum(ip, sizeof(*ip)));

    uint8_t* icmp_buf = frame + sizeof(*eh) + sizeof(*ip);
    bsd_icmp_echo_t* icmp = (bsd_icmp_echo_t*)icmp_buf;
    icmp->type = BSD_ICMP_ECHO;
    icmp->code = 0;
    icmp->cksum = 0;
    icmp->id = bsd_htons(NET_PING_ID);
    icmp->seq = bsd_htons(seq);
    memcpy(icmp_buf + sizeof(*icmp), payload, sizeof(payload));
    icmp->cksum = bsd_htons(bsd_icmp_checksum(icmp_buf, icmp_len));

    int tx_rc = net_frame_emit(iface, dst_host, frame, (uint32_t)frame_len);
    kfree(frame);

    if (tx_rc != 0) {
        spinlock_lock(&g_ping.lock);
        g_ping.waiting = 0;
        spinlock_unlock(&g_ping.lock);
        return 0;
    }

    return seq;
}

int icmp_proto_poll_reply(uint16_t seq, uint32_t* out_rtt_ms)
{
    int done = 0;
    uint32_t rtt = 0;

    spinlock_lock(&g_ping.lock);
    if (g_ping.received && g_ping.await_seq == seq) {
        done = 1;
        rtt = g_ping.last_rtt_ms;
        g_ping.received = 0;
    }
    spinlock_unlock(&g_ping.lock);

    if (done && out_rtt_ms) {
        *out_rtt_ms = rtt;
    }
    return done;
}

void icmp_proto_cancel(uint16_t seq)
{
    spinlock_lock(&g_ping.lock);
    if (g_ping.await_seq == seq) {
        g_ping.waiting = 0;
    }
    spinlock_unlock(&g_ping.lock);
}
