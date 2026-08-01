/*
 * tcpremote.c
 * Remote TCP smoke test: HTTP/1.0 GET to example.com (93.184.216.34:80)
 * via QEMU NAT gateway. Tests the SYN→SYN-ACK→ACK handshake introduced
 * for non-local IP destinations.
 *
 * Usage (inside Rodnix shell):
 *   tcpremote
 */

#include <stdint.h>
#include "posix_syscall.h"

#define FD_STDOUT   1
#define AF_INET     2
#define SOCK_STREAM 1

/* Cloudflare DNS/HTTP: 1.1.1.1 = 0x01010101, port 80 (HTTP 301 redirect) */
#define REMOTE_ADDR 0x01010101u
#define REMOTE_PORT 80u

typedef struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
} sockaddr_in_t;

static void write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) { len++; }
    posix_write(FD_STDOUT, s, len);
}

static void write_uint(uint64_t n)
{
    if (n == 0) { posix_write(FD_STDOUT, "0", 1); return; }
    char buf[20];
    int i = 19;
    buf[i] = '\0';
    while (n > 0 && i > 0) {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    }
    write_str(buf + i);
}

int main(void)
{
    write_str("tcpremote: connecting to 1.1.1.1:80 (Cloudflare)...\n");

    long fd = posix_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        write_str("tcpremote: socket() FAILED\n");
        return 1;
    }

    sockaddr_in_t dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = REMOTE_PORT;
    dst.sin_addr   = REMOTE_ADDR;

    long rc = posix_connect((int)fd, &dst);
    if (rc < 0) {
        write_str("tcpremote: connect() FAILED (SYN timeout or no route)\n");
        posix_close((int)fd);
        return 1;
    }
    write_str("tcpremote: connected OK\n");

    /* Minimal HTTP/1.0 request — no persistent connection */
    const char req[] =
        "GET / HTTP/1.0\r\n"
        "Host: 1.1.1.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    uint64_t req_len = sizeof(req) - 1u;

    long wr = posix_write((int)fd, req, req_len);
    if (wr != (long)req_len) {
        write_str("tcpremote: send() FAILED\n");
        posix_close((int)fd);
        return 1;
    }
    write_str("tcpremote: request sent, reading response...\n");

    /* Read first chunk — expect "HTTP/1." at minimum */
    char buf[256];
    long rd = posix_read((int)fd, buf, sizeof(buf) - 1u);
    if (rd <= 0) {
        write_str("tcpremote: recv() FAILED (no data)\n");
        posix_close((int)fd);
        return 1;
    }
    buf[rd] = '\0';

    write_str("tcpremote: received ");
    write_uint((uint64_t)rd);
    write_str(" bytes\n");

    /* Print first line of response */
    int i = 0;
    while (i < rd && buf[i] != '\n') { i++; }
    buf[i] = '\0';
    write_str("tcpremote: ");
    write_str(buf);
    write_str("\n");

    /* Check for HTTP signature */
    int ok = (rd >= 7 &&
              buf[0] == 'H' && buf[1] == 'T' && buf[2] == 'T' &&
              buf[3] == 'P' && buf[4] == '/');

    posix_close((int)fd);
    write_str(ok ? "tcpremote: PASS\n" : "tcpremote: FAIL (unexpected response)\n");
    return ok ? 0 : 1;
}
