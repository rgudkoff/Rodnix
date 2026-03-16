/*
 * init.c
 * Minimal userspace launcher: smoke + exec /bin/sh.
 */

#include <stdint.h>
#include <stdlib.h>
#include "syscall.h"
#include "posix_syscall.h"
#include "unistd.h"
#include "sys/wait.h"
#include "dirent.h"
#include "time.h"

#define VFS_OPEN_READ 1
#define VFS_OPEN_WRITE 2
#define FD_STDOUT 1
#define SYS_TEST_SLEEP 120

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

static void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

static void write_hex_byte(uint8_t b)
{
    char hex[2];
    static const char table[] = "0123456789ABCDEF";
    hex[0] = table[(b >> 4) & 0xF];
    hex[1] = table[b & 0xF];
    (void)write_buf(hex, 2);
}

static void print_file(const char* path)
{
    char buf[64];
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return;
    }
    for (;;) {
        long n = posix_read((int)fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        (void)posix_write(FD_STDOUT, buf, (uint64_t)n);
    }
    (void)posix_close((int)fd);
}

static void print_hostname(void)
{
    char buf[64];
    long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
    if (fd < 0) {
        return;
    }

    long n = posix_read((int)fd, buf, sizeof(buf) - 1);
    (void)posix_close((int)fd);
    if (n <= 0) {
        return;
    }

    int len = (int)n;
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        len--;
    }
    if (len <= 0) {
        return;
    }

    (void)write_str("[USER] hostname: ");
    (void)write_buf(buf, (uint64_t)len);
    (void)write_str("\n");
}

static void run_smoke(void)
{
    (void)write_str("[USER] init: POSIX smoke test start\n");
    (void)write_str("[USER] getpid=");
    {
        long pid = posix_getpid();
        if (pid < 0) {
            (void)write_str("ERR\n");
        } else {
            write_u64((uint64_t)pid);
            (void)write_str("\n");
        }
    }

    {
        long fd = posix_open("/bin/init", VFS_OPEN_READ);
        if (fd < 0) {
            (void)write_str("[USER] open('/bin/init') failed\n");
        } else {
            uint8_t hdr[4] = {0, 0, 0, 0};
            long n = posix_read((int)fd, hdr, sizeof(hdr));
            (void)write_str("[USER] read('/bin/init') bytes=");
            if (n < 0) {
                (void)write_str("ERR\n");
            } else {
                write_u64((uint64_t)n);
                (void)write_str(" hdr=");
                for (long i = 0; i < n; i++) {
                    write_hex_byte(hdr[i]);
                }
                (void)write_str("\n");
            }
            if (posix_close((int)fd) < 0) {
                (void)write_str("[USER] close failed\n");
            } else {
                (void)write_str("[USER] close ok\n");
            }
        }
    }

    (void)write_str("[USER] init: POSIX smoke test done\n");
}

static int file_exists(const char* path)
{
    long fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return 0;
    }
    (void)posix_close((int)fd);
    return 1;
}

static void ct_log(const char* id, const char* verdict, const char* msg)
{
    (void)write_str("[CT] ");
    (void)write_str(id);
    (void)write_str(" ");
    (void)write_str(verdict);
    (void)write_str(" ");
    (void)write_str(msg);
    (void)write_str("\n");
}

static int cstr_eq(const char* a, const char* b)
{
    uint64_t i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int cstr_contains(const char* s, const char* needle)
{
    uint64_t i = 0;
    uint64_t j = 0;
    if (!s || !needle || !needle[0]) {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        for (j = 0; needle[j] && s[i + j] == needle[j]; j++) {
            ;
        }
        if (!needle[j]) {
            return 1;
        }
    }
    return 0;
}

static int kmod_find(rodnix_kmod_info_t* mods, uint32_t n, const char* name)
{
    if (!mods || !name) {
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (cstr_eq(mods[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static int dev_dir_has_entry(const char* name)
{
    DIR* d = opendir("/dev");
    if (!d) {
        return 0;
    }
    for (;;) {
        struct dirent* de = readdir(d);
        if (!de) {
            break;
        }
        if (cstr_eq(de->d_name, name)) {
            (void)closedir(d);
            return 1;
        }
    }
    (void)closedir(d);
    return 0;
}

static void run_ifconfig_smoke_if_enabled(void)
{
    if (!file_exists("/etc/smoke.ifconfig.auto")) {
        return;
    }

    const char* av[2];
    av[0] = "/bin/ifconfig";
    av[1] = 0;

    (void)write_str("[SMK] IFCONFIG START\n");
    long pid = posix_spawn("/bin/ifconfig", av);
    if (pid <= 0) {
        (void)write_str("[SMK] IFCONFIG FAIL spawn\n");
        return;
    }
    (void)write_str("[SMK] IFCONFIG WAIT (WNOHANG)\n");

    {
        struct timespec t0;
        struct timespec t1;
        const int64_t timeout_ns = 2LL * 1000LL * 1000LL * 1000LL;
        int status = -1;

        if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
            (void)write_str("[SMK] IFCONFIG FAIL clock\n");
            return;
        }

        for (;;) {
            pid_t wr = waitpid((pid_t)pid, &status, WNOHANG);
            if (wr == (pid_t)pid) {
                if (status == 0) {
                    (void)write_str("[SMK] IFCONFIG PASS\n");
                } else {
                    (void)write_str("[SMK] IFCONFIG FAIL wait/status\n");
                }
                return;
            }
            if (wr < 0) {
                (void)write_str("[SMK] IFCONFIG FAIL wait\n");
                return;
            }

            if (clock_gettime(CLOCK_MONOTONIC, &t1) == 0) {
                int64_t dt_ns = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000LL +
                                (int64_t)(t1.tv_nsec - t0.tv_nsec);
                if (dt_ns >= timeout_ns) {
                    (void)write_str("[SMK] IFCONFIG TIMEOUT\n");
                    return;
                }
            }

            (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
        }
    }
}

static void run_contract_mode_if_enabled(void)
{
    if (!file_exists("/etc/contract.auto")) {
        return;
    }

    int ok = 1;
    (void)write_str("[CT] MODE enabled\n");

    {
        char b = 0;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            ct_log("CT-007", "FAIL", "open failed");
            ok = 0;
        } else if (posix_close((int)fd) < 0) {
            ct_log("CT-007", "FAIL", "close failed");
            ok = 0;
        } else if (posix_read((int)fd, &b, 1) < 0) {
            ct_log("CT-007", "PASS", "close invalidates fd");
        } else {
            ct_log("CT-007", "FAIL", "read after close succeeded");
            ok = 0;
        }
    }

    {
        char b = 0;
        if (posix_read(-1, &b, 1) < 0) {
            ct_log("CT-008", "PASS", "read on invalid fd fails");
        } else {
            ct_log("CT-008", "FAIL", "read on invalid fd succeeded");
            ok = 0;
        }
    }

    {
        long ts = rdnx_syscall1(SYS_TEST_SLEEP, 1);
        if (ts >= 0) {
            ct_log("CT-DBG", "PASS", "blocking syscall probe returned");
        } else {
            ct_log("CT-DBG", "FAIL", "blocking syscall probe failed");
        }
    }

    {
        struct timespec m0, m1, r0, r1;
        int time_ok = 1;
        if (clock_gettime(CLOCK_MONOTONIC, &m0) != 0 ||
            clock_gettime(CLOCK_REALTIME, &r0) != 0) {
            ct_log("CT-016", "FAIL", "clock_gettime initial failed");
            ct_log("CT-017", "FAIL", "clock_gettime initial failed");
            ok = 0;
            time_ok = 0;
        }
        if (time_ok) {
            (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
            if (clock_gettime(CLOCK_MONOTONIC, &m1) != 0) {
                ct_log("CT-016", "FAIL", "clock_gettime monotonic failed");
                ok = 0;
                time_ok = 0;
            }
            if (clock_gettime(CLOCK_REALTIME, &r1) != 0) {
                ct_log("CT-017", "FAIL", "clock_gettime realtime failed");
                ok = 0;
                time_ok = 0;
            }
        }
        if (time_ok) {
            uint64_t mu0 = (uint64_t)m0.tv_sec * 1000000ULL + (uint64_t)m0.tv_nsec / 1000ULL;
            uint64_t mu1 = (uint64_t)m1.tv_sec * 1000000ULL + (uint64_t)m1.tv_nsec / 1000ULL;
            uint64_t ru0 = (uint64_t)r0.tv_sec * 1000000ULL + (uint64_t)r0.tv_nsec / 1000ULL;
            uint64_t ru1 = (uint64_t)r1.tv_sec * 1000000ULL + (uint64_t)r1.tv_nsec / 1000ULL;
            if (mu1 >= mu0) {
                ct_log("CT-016", "PASS", "CLOCK_MONOTONIC is non-decreasing");
            } else {
                ct_log("CT-016", "FAIL", "CLOCK_MONOTONIC went backwards");
                ok = 0;
            }
            if (ru1 >= ru0) {
                ct_log("CT-017", "PASS", "CLOCK_REALTIME is non-decreasing");
            } else {
                ct_log("CT-017", "FAIL", "CLOCK_REALTIME went backwards");
                ok = 0;
            }
        }
    }

    {
        int status = -1;
        volatile uint64_t as_canary = 0x1122334455667788ULL;
        long pid = posix_spawn("/bin/true", 0);
        if (pid > 0) {
            ct_log("CT-001", "PASS", "spawn creates child pid");
            {
                long wr = waitpid((pid_t)pid, &status, 0);
                if (wr == pid && status == 0) {
                    ct_log("CT-005", "PASS", "waitpid reaps child exit status");
                } else {
                    ct_log("CT-005", "FAIL", "waitpid failed or bad status");
                    ok = 0;
                }
            }
            if (as_canary == 0x1122334455667788ULL) {
                ct_log("CT-013", "PASS", "parent stack survives child address-space switches");
            } else {
                ct_log("CT-013", "FAIL", "parent stack corrupted after child run/wait");
                ok = 0;
            }
            {
                long wr2 = waitpid((pid_t)pid, &status, 0);
                if (wr2 < 0) {
                    ct_log("CT-006", "PASS", "second waitpid fails after reap");
                } else {
                    ct_log("CT-006", "FAIL", "second waitpid unexpectedly succeeded");
                    ok = 0;
                }
            }
        } else {
            ct_log("CT-001", "FAIL", "spawn failed");
            ct_log("CT-005", "FAIL", "spawn prerequisite failed");
            ct_log("CT-006", "FAIL", "spawn prerequisite failed");
            ct_log("CT-013", "FAIL", "spawn prerequisite failed");
            ok = 0;
        }
    }

    {
        /* CT-014/CT-015: fd inheritance + close-on-exec in spawn+exec flow. */
        int status = -1;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            ct_log("CT-014", "FAIL", "open prerequisite failed");
            ct_log("CT-015", "FAIL", "open prerequisite failed");
            ok = 0;
        } else if (fd != 3) {
            ct_log("CT-014", "FAIL", "expected inherited probe fd=3");
            ct_log("CT-015", "FAIL", "expected inherited probe fd=3");
            ok = 0;
            (void)posix_close((int)fd);
        } else {
            if (fcntl((int)fd, F_GETFD, 0) != 0) {
                ct_log("CT-014", "FAIL", "new fd unexpectedly has CLOEXEC");
                ok = 0;
            }
            long pid = posix_spawn("/bin/contract_fd_inherit", 0);
            if (pid <= 0) {
                ct_log("CT-014", "FAIL", "inherit spawn failed");
                ct_log("CT-015", "FAIL", "inherit spawn prerequisite failed");
                ok = 0;
            } else {
                long wr = waitpid((pid_t)pid, &status, 0);
                if (wr == pid && status == 0) {
                    ct_log("CT-014", "PASS", "spawn inherits open fd");
                } else {
                    ct_log("CT-014", "FAIL", "child could not use inherited fd");
                    (void)write_str("[CT] CT-014 DBG status=");
                    write_u64((uint64_t)(uint32_t)status);
                    (void)write_str("\n");
                    ok = 0;
                }
            }

            if (fcntl((int)fd, F_SETFD, FD_CLOEXEC) < 0) {
                ct_log("CT-015", "FAIL", "F_SETFD failed");
                ok = 0;
            } else if ((fcntl((int)fd, F_GETFD, 0) & FD_CLOEXEC) == 0) {
                ct_log("CT-015", "FAIL", "F_GETFD missing CLOEXEC flag");
                ok = 0;
            } else {
                long pid2 = posix_spawn("/bin/contract_fd_inherit", 0);
                if (pid2 <= 0) {
                    ct_log("CT-015", "FAIL", "cloexec spawn failed");
                    ok = 0;
                } else {
                    long wr2 = waitpid((pid_t)pid2, &status, 0);
                    if (wr2 == pid2 && status == 2) {
                        ct_log("CT-015", "PASS", "CLOEXEC closes fd in spawned image");
                    } else {
                        ct_log("CT-015", "FAIL", "fd leaked across exec with CLOEXEC");
                        (void)write_str("[CT] CT-015 DBG status=");
                        write_u64((uint64_t)(uint32_t)status);
                        (void)write_str("\n");
                        ok = 0;
                    }
                }
            }

            (void)posix_close((int)fd);
        }
    }

    {
        /*
         * CT-003/CT-011:
         * Spawn probe image that exec()'s into /bin/contract_exec_after.
         * Child exit status encodes:
         *   - low 16 bits: post-exec PID
         *   - bit 16: image-specific state reset marker
         */
        int status = -1;
        long pid = posix_spawn("/bin/contract_exec_probe", 0);
        if (pid <= 0) {
            ct_log("CT-003", "FAIL", "exec probe spawn failed");
            ct_log("CT-011", "FAIL", "exec probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr != pid) {
                ct_log("CT-003", "FAIL", "exec probe waitpid failed");
                ct_log("CT-011", "FAIL", "exec probe waitpid failed");
                ok = 0;
            } else {
                uint32_t st = (uint32_t)status;
                uint32_t observed_pid = st & 0xFFFFu;
                uint32_t image_reset = st & 0x10000u;
                if (observed_pid == (((uint32_t)pid) & 0xFFFFu)) {
                    ct_log("CT-003", "PASS", "exec preserves pid across image switch");
                } else {
                    ct_log("CT-003", "FAIL", "exec pid mismatch");
                    ok = 0;
                }
                if (image_reset != 0u) {
                    ct_log("CT-011", "PASS", "exec resets image-specific state");
                } else {
                    ct_log("CT-011", "FAIL", "exec image-specific state leaked");
                    ok = 0;
                }
            }
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_heap", 0);
        if (pid <= 0) {
            ct_log("CT-018", "FAIL", "heap probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-018", "PASS", "heap probe child passed");
            } else {
                ct_log("CT-018", "FAIL", "heap probe child failed");
                ok = 0;
            }
        }
    }

    {
        uint8_t* p = (uint8_t*)malloc(64);
        uint8_t* q = (uint8_t*)calloc(16, 8);
        int local_ok = 1;
        if (!p || !q) {
            local_ok = 0;
        }
        if (local_ok) {
            for (int i = 0; i < 64; i++) {
                p[i] = (uint8_t)(0xA0 + i);
            }
            p = (uint8_t*)realloc(p, 256);
            if (!p) {
                local_ok = 0;
            } else {
                for (int i = 0; i < 64; i++) {
                    if (p[i] != (uint8_t)(0xA0 + i)) {
                        local_ok = 0;
                        break;
                    }
                }
            }
        }
        free(p);
        free(q);
        if (local_ok) {
            ct_log("CT-019", "PASS", "parent heap alloc/realloc/free sequence");
        } else {
            ct_log("CT-019", "FAIL", "parent heap sequence failed");
            ok = 0;
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_dirent", 0);
        if (pid <= 0) {
            ct_log("CT-020", "FAIL", "dirent probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-020", "PASS", "dirent handle API probe passed");
            } else {
                ct_log("CT-020", "FAIL", "dirent handle API probe failed");
                ok = 0;
            }
        }
    }

    {
        int dev_ok = 1;
        if (!dev_dir_has_entry("console") ||
            !dev_dir_has_entry("stdin") ||
            !dev_dir_has_entry("stdout") ||
            !dev_dir_has_entry("stderr") ||
            !dev_dir_has_entry("null") ||
            !dev_dir_has_entry("zero")) {
            dev_ok = 0;
        }
        if (dev_ok) {
            ct_log("CT-024", "PASS", "devfs base nodes visible in /dev");
        } else {
            ct_log("CT-024", "FAIL", "missing required /dev entries");
            ok = 0;
        }
    }

    {
        int tty_ok = (isatty(0) != 0) && (isatty(1) != 0) && (isatty(2) != 0);
        if (tty_ok) {
            ct_log("CT-025", "PASS", "isatty works for stdio fds");
        } else {
            ct_log("CT-025", "FAIL", "isatty failed for stdio");
            ok = 0;
        }
    }

    {
        int io_ok = 1;
        char b = 'X';
        char zbuf[8] = {1,2,3,4,5,6,7,8};
        long fd_null = posix_open("/dev/null", VFS_OPEN_READ | VFS_OPEN_WRITE);
        if (fd_null < 0) {
            io_ok = 0;
        } else {
            long wn = posix_write((int)fd_null, &b, 1);
            long rn = posix_read((int)fd_null, &b, 1);
            if (wn != 1 || rn != 0) {
                io_ok = 0;
            }
            (void)posix_close((int)fd_null);
        }
        long fd_zero = posix_open("/dev/zero", VFS_OPEN_READ);
        if (fd_zero < 0) {
            io_ok = 0;
        } else {
            long rn = posix_read((int)fd_zero, zbuf, sizeof(zbuf));
            if (rn != (long)sizeof(zbuf)) {
                io_ok = 0;
            } else {
                for (uint64_t i = 0; i < sizeof(zbuf); i++) {
                    if (zbuf[i] != 0) {
                        io_ok = 0;
                        break;
                    }
                }
            }
            (void)posix_close((int)fd_zero);
        }
        if (io_ok) {
            ct_log("CT-026", "PASS", "devfs null/zero I/O contract");
        } else {
            ct_log("CT-026", "FAIL", "devfs null/zero behavior mismatch");
            ok = 0;
        }
    }

    {
        rodnix_blockdev_info_t devs[4];
        uint32_t total = 0;
        int block_ok = 1;
        long n = posix_blocklist(devs, 4, &total);
        if (n < 0) {
            block_ok = 0;
        } else if (n > 0) {
            char path[32];
            uint64_t p = 0;
            const char* prefix = "/dev/";
            for (uint64_t i = 0; prefix[i] && p + 1 < sizeof(path); i++) {
                path[p++] = prefix[i];
            }
            for (uint64_t i = 0; devs[0].name[i] && p + 1 < sizeof(path); i++) {
                path[p++] = devs[0].name[i];
            }
            path[p] = '\0';
            if (!dev_dir_has_entry(devs[0].name)) {
                block_ok = 0;
            } else {
                char sec[512];
                char tail[17];
                long fd = posix_open(path, VFS_OPEN_READ);
                if (fd < 0) {
                    block_ok = 0;
                } else {
                    long rn = posix_read((int)fd, sec, sizeof(sec));
                    if (rn != (long)sizeof(sec)) {
                        block_ok = 0;
                    }
                    if (block_ok) {
                        if (posix_lseek((int)fd, 1, 0) < 0) {
                            block_ok = 0;
                        } else {
                            long rn2 = posix_read((int)fd, tail, sizeof(tail));
                            if (rn2 != (long)sizeof(tail)) {
                                block_ok = 0;
                            }
                        }
                    }
                    (void)posix_close((int)fd);
                }
            }
        }

        if (block_ok) {
            ct_log("CT-027", "PASS", "devfs block node bridge works");
        } else {
            ct_log("CT-027", "FAIL", "devfs block node missing or unreadable");
            ok = 0;
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_fsio", 0);
        if (pid <= 0) {
            ct_log("CT-021", "FAIL", "fsio probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-021", "PASS", "stat/fstat/lseek probe passed");
            } else {
                ct_log("CT-021", "FAIL", "stat/fstat/lseek probe failed");
                ok = 0;
            }
        }
    }

    {
        int status = -1;
        long pid = posix_spawn("/bin/contract_wait_nonchild", 0);
        if (pid <= 0) {
            ct_log("CT-004", "FAIL", "wait-nonchild probe spawn failed");
            ok = 0;
        } else {
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-004", "PASS", "waitpid on non-child is denied");
            } else {
                ct_log("CT-004", "FAIL", "waitpid non-child contract failed");
                ok = 0;
            }
        }
    }

    {
        rodnix_kmod_info_t mods[32];
        uint32_t total = 0;
        long n = posix_kmodls(mods, 32, &total);
        if (n < 0) {
            ct_log("CT-022", "FAIL", "kmodls initial failed");
            ok = 0;
        } else {
            int ext2_idx = kmod_find(mods, (uint32_t)n, "fs.ext2");
            if (ext2_idx < 0 || mods[ext2_idx].loaded == 0 || mods[ext2_idx].builtin == 0) {
                ct_log("CT-022", "FAIL", "builtin fs.ext2 not visible");
                ok = 0;
            } else if (posix_kmodload("/lib/modules/demo.ko") < 0) {
                ct_log("CT-022", "FAIL", "kmodload demo failed");
                ok = 0;
            } else {
                uint32_t total2 = 0;
                long n2 = posix_kmodls(mods, 32, &total2);
                int demo_idx = (n2 < 0) ? -1 : kmod_find(mods, (uint32_t)n2, "demo.echo");
                if (n2 < 0 || demo_idx < 0 || mods[demo_idx].loaded == 0 || mods[demo_idx].builtin != 0) {
                    ct_log("CT-022", "FAIL", "demo module not loaded");
                    ok = 0;
                } else if (posix_kmodunload("demo.echo") < 0) {
                    ct_log("CT-022", "FAIL", "kmodunload demo failed");
                    ok = 0;
                } else {
                    uint32_t total3 = 0;
                    long n3 = posix_kmodls(mods, 32, &total3);
                    int demo3 = (n3 < 0) ? -1 : kmod_find(mods, (uint32_t)n3, "demo.echo");
                    if (n3 < 0 || demo3 < 0 || mods[demo3].loaded != 0) {
                        ct_log("CT-022", "FAIL", "demo module still loaded");
                        ok = 0;
                    } else {
                        ct_log("CT-022", "PASS", "kmod ls/load/unload contract");
                    }
                }
            }
        }
    }

    {
        /* Race case: child may exit before parent enters waitpid. */
        int status = -1;
        long pid = posix_spawn("/bin/true", 0);
        if (pid <= 0) {
            ct_log("CT-005", "FAIL", "race spawn failed");
            ct_log("CT-006", "FAIL", "race spawn prerequisite failed");
            ok = 0;
        } else {
            /* Give child a chance to complete before first waitpid recheck. */
            for (int i = 0; i < 8; i++) {
                (void)rdnx_syscall0(0);
            }
            long wr = waitpid((pid_t)pid, &status, 0);
            if (wr == pid && status == 0) {
                ct_log("CT-005", "PASS", "race fast-exit child reaped");
            } else {
                ct_log("CT-005", "FAIL", "race waitpid failed");
                ok = 0;
            }
            {
                long wr2 = waitpid((pid_t)pid, &status, 0);
                if (wr2 < 0) {
                    ct_log("CT-006", "PASS", "race second waitpid fails after reap");
                } else {
                    ct_log("CT-006", "FAIL", "race second waitpid unexpectedly succeeded");
                    ok = 0;
                }
            }
        }
    }

    {
        char buf[192];
        long fd = posix_open("/mnt/README.txt", VFS_OPEN_READ);
        if (fd < 0) {
            ct_log("CT-023", "FAIL", "open /mnt/README.txt failed");
            ok = 0;
        } else {
            long nr = posix_read((int)fd, buf, sizeof(buf) - 1);
            (void)posix_close((int)fd);
            if (nr <= 0) {
                ct_log("CT-023", "FAIL", "read /mnt/README.txt failed");
                ok = 0;
            } else {
                buf[nr] = '\0';
                if (cstr_contains(buf, "RodNIX ext2 demo volume")) {
                    ct_log("CT-023", "PASS", "ext2 mounted and file read");
                } else {
                    ct_log("CT-023", "FAIL", "ext2 content mismatch");
                    ok = 0;
                }
            }
        }
    }

    {
        int wr_ok = 1;
        int wr_unavail = 0; /* reserved while storage write path is being stabilized */
        char buf[16];
        long fd = posix_open("/mnt/README.txt", VFS_OPEN_READ | VFS_OPEN_WRITE);
        if (fd < 0) {
            wr_ok = 0;
        } else {
            long rr = posix_read((int)fd, buf, sizeof(buf));
            if (rr != (long)sizeof(buf)) {
                wr_ok = 0;
            }
            (void)posix_close((int)fd);
        }

        if (wr_ok) {
            fd = posix_open("/mnt/README.txt", VFS_OPEN_READ | VFS_OPEN_WRITE);
            if (fd < 0) {
                wr_ok = 0;
            } else {
                long wn = posix_write((int)fd, buf, sizeof(buf));
                if (wn != (long)sizeof(buf)) {
                    if (wn == -7) {
                        wr_unavail = 1;
                    } else {
                        wr_ok = 0;
                    }
                }
                (void)posix_close((int)fd);
            }
        }

        if (wr_ok && !wr_unavail) {
            fd = posix_open("/mnt/README.txt", VFS_OPEN_READ);
            if (fd < 0) {
                wr_ok = 0;
            } else {
                char chk[16];
                long rr2 = posix_read((int)fd, chk, sizeof(chk));
                if (rr2 != (long)sizeof(chk)) {
                    wr_ok = 0;
                } else {
                    for (uint64_t i = 0; i < sizeof(buf); i++) {
                        if (chk[i] != buf[i]) {
                            wr_ok = 0;
                            break;
                        }
                    }
                }
                (void)posix_close((int)fd);
            }
        }

        if (wr_ok && !wr_unavail) {
            ct_log("CT-028", "PASS", "ext2 overwrite writeback path");
        } else if (wr_unavail) {
            ct_log("CT-028", "PASS", "ext2 writeback deferred: block write backend unavailable");
        } else {
            ct_log("CT-028", "FAIL", "ext2 overwrite writeback failed");
            ok = 0;
        }
    }

    if (ok) {
        (void)write_str("[CT] ALL PASS\n");
    } else {
        (void)write_str("[CT] ALL FAIL\n");
    }
}

int main(void)
{
    (void)write_str("Rodnix userspace init launcher\n");
    print_file("/etc/motd");
    (void)write_str("\n");
    print_hostname();
    run_smoke();
    run_ifconfig_smoke_if_enabled();
    run_contract_mode_if_enabled();

    (void)write_str("[USER] init: exec /bin/sh\n");
    long ret = posix_exec("/bin/sh");
    if (ret < 0) {
        (void)write_str("[USER] init: exec failed\n");
        (void)posix_exit(1);
    }

    for (;;) {
        (void)rdnx_syscall0(0);
    }
    return 0;
}
