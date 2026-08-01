/*
 * wget.c — minimal HTTP/1.0 downloader for RodNIX.
 *
 * Usage:
 *   wget http://hostname[:port]/path
 *   wget -O outfile http://hostname[:port]/path
 *   wget -O - http://hostname[:port]/path     (stdout)
 *
 * Features:
 *   • DNS resolution via SLIRP (10.0.2.3:53)
 *   • HTTP/1.0 GET with Connection: close
 *   • Follows one HTTP 301/302 redirect
 *   • Streams response body to stdout or file
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "posix_syscall.h"
#include "dns.h"

#define AF_INET     2
#define SOCK_STREAM 1

#define HTTP_RECV_BUFSZ  4096
#define HTTP_MAX_HDRSZ   8192   /* max header block we buffer */
#define MAX_REDIRECTS    3

/* Kernel-compatible sockaddr_in (matches net/socket.h) */
typedef struct {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
} wget_sa_t;

/* -----------------------------------------------------------------------
 * Tiny string helpers (no sprintf dependency for integers)
 * --------------------------------------------------------------------- */

static int wg_strlen(const char* s)
{
    int n = 0;
    while (s[n]) { n++; }
    return n;
}

static void wg_puts(const char* s)
{
    posix_write(2 /* stderr */, s, (uint64_t)wg_strlen(s));
}

/* -----------------------------------------------------------------------
 * URL parser
 *
 * Parses "http://host[:port]/path" into components.
 * host/path are written into caller-supplied buffers.
 * Returns 0 on success, -1 on error.
 * --------------------------------------------------------------------- */

static int parse_url(const char* url,
                     char*    host,  int host_cap,
                     uint16_t* port,
                     char*    path,  int path_cap)
{
    /* Must start with http:// (https:// not supported) */
    if (strncmp(url, "https://", 8) == 0) {
        return -2; /* special: HTTPS not supported */
    }
    if (strncmp(url, "http://", 7) != 0) {
        return -1;
    }
    const char* p = url + 7;

    /* Find end of host[:port] — either '/' or '\0' */
    const char* slash = strchr(p, '/');
    int hostport_len;
    if (slash) {
        hostport_len = (int)(slash - p);
    } else {
        hostport_len = wg_strlen(p);
        slash = p + hostport_len; /* points to '\0' */
    }

    /* Split host and optional :port */
    const char* colon = NULL;
    for (int i = 0; i < hostport_len; i++) {
        if (p[i] == ':') { colon = p + i; break; }
    }

    int host_len;
    if (colon) {
        host_len = (int)(colon - p);
        /* parse port number */
        long pval = 0;
        for (const char* q = colon + 1; q < p + hostport_len; q++) {
            if (*q < '0' || *q > '9') { return -1; }
            pval = pval * 10 + (*q - '0');
        }
        if (pval <= 0 || pval > 65535) { return -1; }
        *port = (uint16_t)pval;
    } else {
        host_len = hostport_len;
        *port = 80;
    }

    if (host_len <= 0 || host_len >= host_cap) { return -1; }
    memcpy(host, p, (size_t)host_len);
    host[host_len] = '\0';

    /* Path: everything from slash onwards, default to "/" */
    int plen = wg_strlen(slash);
    if (plen == 0) {
        if (path_cap < 2) { return -1; }
        path[0] = '/'; path[1] = '\0';
    } else {
        if (plen >= path_cap) { plen = path_cap - 1; }
        memcpy(path, slash, (size_t)plen);
        path[plen] = '\0';
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * Extract a header value (case-insensitive key match).
 * src_hdr must be NUL-terminated header block.
 * Returns pointer into src_hdr at the start of the value, or NULL.
 * --------------------------------------------------------------------- */

static const char* hdr_find(const char* hdr_block, const char* key)
{
    int klen = wg_strlen(key);
    const char* p = hdr_block;
    while (*p) {
        /* compare key case-insensitively */
        int match = 1;
        for (int i = 0; i < klen; i++) {
            char a = p[i], b = key[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match && p[klen] == ':') {
            const char* v = p + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            return v;
        }
        /* skip to next line */
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Core HTTP fetch
 *
 * Connects to ip:port, sends GET path, streams body to out_fd.
 * Returns HTTP status code (200, 301, …) or -1 on error.
 * On redirect, fills redirect_url (cap = redir_cap) and returns 3xx.
 * --------------------------------------------------------------------- */

static int http_fetch(uint32_t ip, uint16_t port,
                      const char* host, const char* path,
                      int out_fd,
                      char* redirect_url, int redir_cap)
{
    /* Connect */
    long fd = posix_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        wg_puts("wget: socket() failed\n");
        return -1;
    }

    wget_sa_t dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = port;
    dst.sin_addr   = ip;
    if (posix_connect((int)fd, &dst) < 0) {
        char ipbuf[20];
        dns_ip_to_str(ip, ipbuf);
        wg_puts("wget: connect() failed to ");
        wg_puts(ipbuf);
        wg_puts("\n");
        posix_close((int)fd);
        return -1;
    }

    /* Send HTTP/1.0 GET request */
    char req[1024];
    int reqlen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: RodNIX-wget/0.1\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);
    if (reqlen <= 0 || reqlen >= (int)sizeof(req)) {
        posix_close((int)fd);
        return -1;
    }
    if (posix_write((int)fd, req, (uint64_t)reqlen) != reqlen) {
        wg_puts("wget: send failed\n");
        posix_close((int)fd);
        return -1;
    }

    /* Read response, buffer until we have the full header block */
    static char hdr_buf[HTTP_MAX_HDRSZ + HTTP_RECV_BUFSZ];
    int hdr_total = 0;
    int header_done = 0;
    int status = -1;

    while (!header_done) {
        long rd = posix_read((int)fd,
                             hdr_buf + hdr_total,
                             (uint64_t)(HTTP_MAX_HDRSZ - hdr_total));
        if (rd <= 0) {
            break; /* EOF before headers complete — malformed */
        }
        hdr_total += (int)rd;

        /* Search for \r\n\r\n */
        for (int i = 0; i + 3 < hdr_total; i++) {
            if (hdr_buf[i]   == '\r' && hdr_buf[i+1] == '\n' &&
                hdr_buf[i+2] == '\r' && hdr_buf[i+3] == '\n') {
                /* NUL-terminate the header block for easy parsing */
                hdr_buf[i] = '\0';

                /* Parse status line: "HTTP/x.x NNN ..." */
                const char* sp = strchr(hdr_buf, ' ');
                if (sp) {
                    status = (int)strtol(sp + 1, NULL, 10);
                }

                /* For redirects, extract Location */
                if (redirect_url && (status == 301 || status == 302 ||
                                     status == 303 || status == 307 ||
                                     status == 308)) {
                    const char* loc = hdr_find(hdr_buf, "location");
                    if (loc) {
                        int llen = 0;
                        while (loc[llen] && loc[llen] != '\r' && loc[llen] != '\n') {
                            llen++;
                        }
                        if (llen > 0 && llen < redir_cap) {
                            memcpy(redirect_url, loc, (size_t)llen);
                            redirect_url[llen] = '\0';
                        }
                    }
                }

                /* Body starts right after \r\n\r\n */
                int body_start = i + 4;
                int body_avail = hdr_total - body_start;

                /* Stream body to output — only for 200 (not for redirects) */
                if (body_avail > 0 && out_fd >= 0 &&
                    (status == 200 || status <= 0)) {
                    posix_write(out_fd, hdr_buf + body_start,
                                (uint64_t)body_avail);
                }
                header_done = 1;
                break;
            }
        }
        if (hdr_total >= HTTP_MAX_HDRSZ && !header_done) {
            wg_puts("wget: response headers too large\n");
            posix_close((int)fd);
            return -1;
        }
    }

    if (!header_done) {
        wg_puts("wget: incomplete HTTP headers\n");
        posix_close((int)fd);
        return -1;
    }

    /* Stream remaining body */
    if (out_fd >= 0 && (status == 200 || status <= 0)) {
        char chunk[HTTP_RECV_BUFSZ];
        long rd;
        while ((rd = posix_read((int)fd, chunk, sizeof(chunk))) > 0) {
            posix_write(out_fd, chunk, (uint64_t)rd);
        }
    }

    posix_close((int)fd);
    return status;
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */

int main(int argc, char* argv[])
{
    const char* out_path = NULL; /* NULL = stdout */
    const char* url      = NULL;

    /* Argument parsing */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            out_path = argv[i] + 2;
        } else if (strncmp(argv[i], "http://", 7) == 0 ||
                   strncmp(argv[i], "https://", 8) == 0) {
            url = argv[i];
        } else {
            fprintf(stderr, "wget: unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!url) {
        fprintf(stderr, "Usage: wget [-O outfile] http://host[:port]/path\n");
        return 1;
    }

    /* Determine output fd */
    int out_fd = 1; /* stdout */
    int out_fd_owned = 0;

    if (out_path && strcmp(out_path, "-") != 0) {
        out_fd = (int)posix_open(out_path,
                                 O_WRONLY | O_CREAT | O_TRUNC);
        if (out_fd < 0) {
            fprintf(stderr, "wget: cannot open '%s' for writing\n", out_path);
            return 1;
        }
        out_fd_owned = 1;
    }

    /* Main fetch loop (follow up to MAX_REDIRECTS redirects) */
    char cur_url[1024];
    strncpy(cur_url, url, sizeof(cur_url) - 1);
    cur_url[sizeof(cur_url) - 1] = '\0';

    int status = -1;

    for (int redir = 0; redir <= MAX_REDIRECTS; redir++) {
        char host[256], path[512];
        uint16_t port = 80;

        int pret = parse_url(cur_url, host, sizeof(host),
                             &port, path, sizeof(path));
        if (pret == -2) {
            fprintf(stderr, "wget: HTTPS is not supported (only HTTP)\n");
            status = -1;
            break;
        } else if (pret != 0) {
            fprintf(stderr, "wget: invalid URL: %s\n", cur_url);
            status = -1;
            break;
        }

        /* Resolve hostname → IP */
        uint32_t ip = 0;
        fprintf(stderr, "wget: resolving %s... ", host);
        if (dns_resolve(host, &ip) != 0) {
            fprintf(stderr, "FAILED\n");
            status = -1;
            break;
        }
        char ipbuf[20];
        dns_ip_to_str(ip, ipbuf);
        fprintf(stderr, "%s\n", ipbuf);

        fprintf(stderr, "wget: connecting to %s:%u... ", host, (unsigned)port);

        char redir_url[1024] = {0};
        status = http_fetch(ip, port, host, path, out_fd,
                            redir_url, (int)sizeof(redir_url));
        if (status < 0) {
            break;
        }

        fprintf(stderr, "HTTP %d\n", status);

        /* Follow redirect? */
        if ((status == 301 || status == 302 || status == 303 ||
             status == 307 || status == 308) && redir_url[0]) {
            if (redir < MAX_REDIRECTS) {
                fprintf(stderr, "wget: redirect -> %s\n", redir_url);
                strncpy(cur_url, redir_url, sizeof(cur_url) - 1);
                cur_url[sizeof(cur_url) - 1] = '\0';
                /* Only stream body on the final response */
                if (out_fd_owned || out_fd == 1) {
                    /* don't write redirect bodies */
                }
                continue;
            } else {
                fprintf(stderr, "wget: too many redirects\n");
                status = -1;
            }
        }
        break; /* success or non-redirect status */
    }

    if (out_fd_owned) {
        posix_close(out_fd);
    }

    return (status >= 200 && status < 400) ? 0 : 1;
}
