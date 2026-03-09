#ifndef _RODNIX_BSD_MBUF_H
#define _RODNIX_BSD_MBUF_H

#include <stddef.h>
#include <stdint.h>

#define BSD_MBUF_DATA_MAX 2048u

/* FreeBSD-style allocation hints/types (reduced set). */
#define BSD_M_NOWAIT 0x0001
#define BSD_M_WAITOK 0x0002

#define BSD_MT_DATA 1

#define BSD_M_PKTHDR 0x0001

typedef struct bsd_pkthdr {
    void* rcvif;
    int len;
    uint32_t flowid;
} bsd_pkthdr_t;

typedef struct bsd_mbuf {
    struct bsd_mbuf* m_next;
    uint8_t* m_data;
    uint32_t m_len;
    uint16_t m_flags;
    uint8_t m_type;
    bsd_pkthdr_t m_pkthdr;
    uint8_t m_dat[BSD_MBUF_DATA_MAX];
} bsd_mbuf_t;

#define bsd_mtod(_m, _t) ((_t)((_m)->m_data))

bsd_mbuf_t* bsd_m_get(int how, short type);
bsd_mbuf_t* bsd_m_gethdr(int how, short type);
bsd_mbuf_t* bsd_m_free(bsd_mbuf_t* m);
void bsd_m_freem(bsd_mbuf_t* m);
int bsd_m_append(bsd_mbuf_t* m, const void* data, size_t len);
int bsd_m_copydata(const bsd_mbuf_t* m, size_t off, size_t len, void* out);

/* FreeBSD-style aliases for faster driver porting. */
typedef bsd_pkthdr_t pkthdr_t;
typedef bsd_mbuf_t mbuf_t;
typedef bsd_mbuf_t mbuf;

#define M_NOWAIT BSD_M_NOWAIT
#define M_WAITOK BSD_M_WAITOK
#define MT_DATA BSD_MT_DATA
#define M_PKTHDR BSD_M_PKTHDR

#define mtod(_m, _t) bsd_mtod((_m), (_t))
#define m_get(_how, _type) bsd_m_get((_how), (_type))
#define m_gethdr(_how, _type) bsd_m_gethdr((_how), (_type))
#define m_free(_m) bsd_m_free((_m))
#define m_freem(_m) bsd_m_freem((_m))
#define m_append(_m, _data, _len) bsd_m_append((_m), (_data), (_len))
#define m_copydata(_m, _off, _len, _out) bsd_m_copydata((_m), (_off), (_len), (_out))

#endif /* _RODNIX_BSD_MBUF_H */
