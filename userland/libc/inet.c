/*
 * SPDX-License-Identifier: ISC
 *
 * IPv4 text conversion path adapted for RodNIX from an ISC-licensed inet
 * implementation originally written by Paul Vixie.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

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

uint16_t htons(uint16_t host16)
{
    return bswap16(host16);
}

uint16_t ntohs(uint16_t net16)
{
    return bswap16(net16);
}

uint32_t htonl(uint32_t host32)
{
    return bswap32(host32);
}

uint32_t ntohl(uint32_t net32)
{
    return bswap32(net32);
}

static const char* inet_ntop4(const uint8_t* src, char* dst, socklen_t size)
{
    static const char fmt[] = "%u.%u.%u.%u";
    char tmp[sizeof("255.255.255.255")];
    int l = snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]);
    if (l <= 0 || (socklen_t)l >= size) {
        errno = ENOSPC;
        return NULL;
    }
    strncpy(dst, tmp, size - 1);
    dst[size - 1] = '\0';
    return dst;
}

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size)
{
    if (!src || !dst || size == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (af != AF_INET) {
        errno = EINVAL;
        return NULL;
    }
    return inet_ntop4((const uint8_t*)src, dst, size);
}

char* inet_ntoa(struct in_addr in)
{
    static char ret[18];
    ret[0] = '\0';
    (void)inet_ntop(AF_INET, &in, ret, sizeof(ret));
    return ret;
}

char* inet_ntoa_r(struct in_addr in, char* buf, socklen_t size)
{
    if (!buf || size == 0) {
        errno = EINVAL;
        return NULL;
    }
    (void)inet_ntop(AF_INET, &in, buf, size);
    return buf;
}
