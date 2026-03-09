#include "bsd_mbuf.h"
#include "../common/heap.h"
#include "../../include/common.h"

static bsd_mbuf_t* bsd_m_alloc(short type, uint16_t flags)
{
    bsd_mbuf_t* m = (bsd_mbuf_t*)kmalloc(sizeof(bsd_mbuf_t));
    if (!m) {
        return NULL;
    }
    memset(m, 0, sizeof(*m));
    m->m_type = (uint8_t)type;
    m->m_flags = flags;
    m->m_data = m->m_dat;
    return m;
}

bsd_mbuf_t* bsd_m_get(int how, short type)
{
    (void)how;
    return bsd_m_alloc(type, 0);
}

bsd_mbuf_t* bsd_m_gethdr(int how, short type)
{
    (void)how;
    return bsd_m_alloc(type, BSD_M_PKTHDR);
}

bsd_mbuf_t* bsd_m_free(bsd_mbuf_t* m)
{
    if (!m) {
        return NULL;
    }
    bsd_mbuf_t* next = m->m_next;
    kfree(m);
    return next;
}

void bsd_m_freem(bsd_mbuf_t* m)
{
    while (m) {
        m = bsd_m_free(m);
    }
}

int bsd_m_append(bsd_mbuf_t* m, const void* data, size_t len)
{
    if (!m || !data || len == 0 || len > BSD_MBUF_DATA_MAX) {
        return -1;
    }
    memcpy(m->m_data, data, len);
    m->m_len = (uint32_t)len;
    if ((m->m_flags & BSD_M_PKTHDR) != 0) {
        m->m_pkthdr.len = (int)len;
    }
    return 0;
}

int bsd_m_copydata(const bsd_mbuf_t* m, size_t off, size_t len, void* out)
{
    if (!m || !out || len == 0) {
        return -1;
    }

    size_t skip = off;
    size_t remaining = len;
    uint8_t* dst = (uint8_t*)out;

    while (m && skip >= m->m_len) {
        skip -= m->m_len;
        m = m->m_next;
    }

    while (m && remaining > 0) {
        size_t avail = m->m_len - skip;
        size_t take = (remaining < avail) ? remaining : avail;
        memcpy(dst, m->m_data + skip, take);
        dst += take;
        remaining -= take;
        skip = 0;
        m = m->m_next;
    }

    return (remaining == 0) ? 0 : -1;
}
