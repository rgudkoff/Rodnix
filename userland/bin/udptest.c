/*
 * udptest.c
 * Minimal UDP loopback smoke test via POSIX socket syscalls.
 */

#include <stdint.h>
#include "posix_syscall.h"

#define FD_STDOUT 1
#define AF_INET 2
#define SOCK_DGRAM 2
#define NET_LOOPBACK_ADDR 0x7F000001u

typedef struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
} sockaddr_in_t;

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

int main(void)
{
    const char* payload = "udp-loopback-ok";
    char rx[64];
    sockaddr_in_t dst = {0};
    sockaddr_in_t src = {0};

    long srv = posix_socket(AF_INET, SOCK_DGRAM, 0);
    long cli = posix_socket(AF_INET, SOCK_DGRAM, 0);
    if (srv < 0 || cli < 0) {
        (void)write_str("udptest: socket failed\n");
        return 1;
    }

    dst.sin_family = AF_INET;
    dst.sin_port = 100;
    dst.sin_addr = NET_LOOPBACK_ADDR;
    if (posix_bind((int)srv, &dst) < 0) {
        (void)write_str("udptest: bind failed\n");
        (void)posix_close((int)srv);
        (void)posix_close((int)cli);
        return 1;
    }

    long wr = posix_sendto((int)cli, payload, 15, 0, &dst, sizeof(dst));
    if (wr != 15) {
        (void)write_str("udptest: sendto failed\n");
        (void)posix_close((int)srv);
        (void)posix_close((int)cli);
        return 1;
    }

    long rd = posix_recvfrom((int)srv, rx, sizeof(rx), 0, &src, 500);
    if (rd != 15) {
        (void)write_str("udptest: recvfrom failed\n");
        (void)posix_close((int)srv);
        (void)posix_close((int)cli);
        return 1;
    }

    rx[15] = '\0';
    if (rx[0] != 'u' || rx[1] != 'd' || rx[2] != 'p') {
        (void)write_str("udptest: payload mismatch\n");
        (void)posix_close((int)srv);
        (void)posix_close((int)cli);
        return 1;
    }

    (void)posix_close((int)srv);
    (void)posix_close((int)cli);
    (void)write_str("udptest: PASS\n");
    return 0;
}
