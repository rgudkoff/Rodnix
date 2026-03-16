/*
 * tcptest.c
 * Local TCP smoke test with step-by-step trace output.
 */

#include <stdint.h>
#include "posix_syscall.h"

#define FD_STDOUT       1
#define AF_INET         2
#define SOCK_STREAM     1
#define NET_LOCAL_ADDR  0x0A00020Fu
#define TCP_TEST_PORT   4321u

typedef struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
} sockaddr_in_t;

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) { len++; }
    return posix_write(FD_STDOUT, s, len);
}

/* ------------------------------------------------------------------ server */

static int run_server(int srv_fd)
{
    sockaddr_in_t peer = {0};

    (void)write_str("[S] accept...\n");
    long conn_fd = posix_accept(srv_fd, &peer, 5000u);
    if (conn_fd < 0) {
        (void)write_str("[S] accept FAILED\n");
        (void)posix_close(srv_fd);
        return 1;
    }
    (void)write_str("[S] accept OK\n");

    char buf[64];
    (void)write_str("[S] read...\n");
    long rd = posix_read((int)conn_fd, buf, sizeof(buf) - 1u);
    if (rd <= 0) {
        (void)write_str("[S] read FAILED\n");
        (void)posix_close((int)conn_fd);
        (void)posix_close(srv_fd);
        return 1;
    }
    (void)write_str("[S] read OK\n");

    (void)write_str("[S] write (echo)...\n");
    long wr = posix_write((int)conn_fd, buf, (uint64_t)rd);
    if (wr != rd) {
        (void)write_str("[S] write FAILED\n");
        (void)posix_close((int)conn_fd);
        (void)posix_close(srv_fd);
        return 1;
    }
    (void)write_str("[S] write OK\n");

    (void)posix_close((int)conn_fd);
    (void)posix_close(srv_fd);
    (void)write_str("[S] done\n");
    return 0;
}

/* ------------------------------------------------------------------ client */

static int run_client(void)
{
    long cli_fd = posix_socket(AF_INET, SOCK_STREAM, 0);
    if (cli_fd < 0) {
        (void)write_str("[C] socket FAILED\n");
        return 1;
    }
    (void)write_str("[C] socket OK\n");

    sockaddr_in_t dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = TCP_TEST_PORT;
    dst.sin_addr   = NET_LOCAL_ADDR;

    (void)write_str("[C] connect...\n");
    long rc = posix_connect((int)cli_fd, &dst);
    if (rc < 0) {
        (void)write_str("[C] connect FAILED\n");
        (void)posix_close((int)cli_fd);
        return 1;
    }
    (void)write_str("[C] connect OK\n");

    const char* msg = "hello-tcp";
    const uint64_t msglen = 9u;

    (void)write_str("[C] write...\n");
    long wr = posix_write((int)cli_fd, msg, msglen);
    if (wr != (long)msglen) {
        (void)write_str("[C] write FAILED\n");
        (void)posix_close((int)cli_fd);
        return 1;
    }
    (void)write_str("[C] write OK\n");

    char rx[16];
    (void)write_str("[C] read...\n");
    long rd = posix_read((int)cli_fd, rx, sizeof(rx));
    if (rd != (long)msglen) {
        (void)write_str("[C] read FAILED\n");
        (void)posix_close((int)cli_fd);
        return 1;
    }
    (void)write_str("[C] read OK\n");

    int ok = 1;
    for (uint64_t i = 0; i < msglen; i++) {
        if (rx[i] != msg[i]) { ok = 0; break; }
    }
    (void)write_str(ok ? "[C] verify OK\n" : "[C] verify FAIL\n");
    (void)write_str("[C] closing...\n");
    (void)posix_close((int)cli_fd);
    (void)write_str("[C] closed\n");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ main  */

int main(void)
{
    long srv_fd = posix_socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) {
        (void)write_str("tcptest: socket failed\n");
        return 1;
    }

    sockaddr_in_t srv_addr = {0};
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = TCP_TEST_PORT;
    srv_addr.sin_addr   = NET_LOCAL_ADDR;

    if (posix_bind((int)srv_fd, (const void*)&srv_addr) < 0) {
        (void)write_str("tcptest: bind failed\n");
        (void)posix_close((int)srv_fd);
        return 1;
    }

    if (posix_listen((int)srv_fd, 4) < 0) {
        (void)write_str("tcptest: listen failed\n");
        (void)posix_close((int)srv_fd);
        return 1;
    }

    (void)write_str("tcptest: forking...\n");
    long pid = posix_fork();
    if (pid < 0) {
        (void)write_str("tcptest: fork failed\n");
        (void)posix_close((int)srv_fd);
        return 1;
    }

    if (pid == 0) {
        (void)posix_close((int)srv_fd);
        int r = run_client();
        (void)write_str("[C] exiting\n");
        posix_exit(r);
    }

    int srv_rc = run_server((int)srv_fd);

    (void)write_str("tcptest: waitpid...\n");
    int status = -1;
    long wr = posix_waitpid(pid, &status);
    if (wr != pid) {
        (void)write_str("tcptest: waitpid failed\n");
        return 1;
    }

    if (srv_rc != 0 || status != 0) {
        (void)write_str("tcptest: FAIL\n");
        return 1;
    }

    (void)write_str("tcptest: PASS\n");
    return 0;
}
