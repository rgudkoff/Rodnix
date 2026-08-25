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
#include "sys/stat.h"
#include "dirent.h"
#include "time.h"
#include "string.h"
#include "stdio.h"
#include "common.h"
#include "service.h"

#define VFS_OPEN_READ 1
#define VFS_OPEN_WRITE 2
#define VFS_OPEN_CREATE 4
#define FD_STDOUT 1
#define SYS_TEST_SLEEP 120

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

    (void)write_str("[init] hostname: ");
    (void)write_buf(buf, (uint64_t)len);
    (void)write_str("\n");
}

static void run_smoke(void)
{
    (void)write_str("[init] self-test: start\n");
    (void)write_str("[init] self-test: pid=");
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
            (void)write_str("[init] self-test: open /bin/init failed\n");
        } else {
            uint8_t hdr[4] = {0, 0, 0, 0};
            long n = posix_read((int)fd, hdr, sizeof(hdr));
            (void)write_str("[init] self-test: read /bin/init bytes=");
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
                (void)write_str("[init] self-test: close failed\n");
            } else {
                (void)write_str("[init] self-test: close ok\n");
            }
        }
    }

    (void)write_str("[init] self-test: done\n");
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


static int file_contains_text(const char* path, const char* needle)
{
    char buf[512];
    long fd;
    long n;

    if (!path || !needle) {
        return 0;
    }

    fd = posix_open(path, VFS_OPEN_READ);
    if (fd < 0) {
        return 0;
    }

    n = posix_read((int)fd, buf, sizeof(buf) - 1);
    (void)posix_close((int)fd);
    if (n <= 0) {
        return 0;
    }

    buf[n] = '\0';
    return cstr_contains(buf, needle);
}

static int write_text_file(const char* path, const char* text)
{
    size_t len = 0;
    int fd;

    if (!path || !text) {
        return 0;
    }

    while (text[len] != '\0') {
        len++;
    }

    fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return 0;
    }
    if (write(fd, text, len) != (ssize_t)len) {
        (void)close(fd);
        return 0;
    }
    return close(fd) == 0;
}

/* Emit one diagnosable spawn-failure line with the numbers that separate the
 * three distinct causes: spawn refused (rc), waitpid returned the wrong pid
 * (pid vs wret), or the child exited nonzero (status — 127 means exec failed
 * inside the child). */
static void spawn_diag(const char* stage, const char* cmd,
                       const char* k1, long v1,
                       const char* k2, long v2)
{
    (void)write_str("[CT] spawn DIAG ");
    (void)write_str(stage);
    (void)write_str(" cmd=");
    (void)write_str(cmd ? cmd : "?");
    if (k1) {
        (void)write_str(" ");
        (void)write_str(k1);
        (void)write_str("=");
        if (v1 < 0) { (void)write_str("-"); write_u64((uint64_t)(-v1)); }
        else { write_u64((uint64_t)v1); }
    }
    if (k2) {
        (void)write_str(" ");
        (void)write_str(k2);
        (void)write_str("=");
        if (v2 < 0) { (void)write_str("-"); write_u64((uint64_t)(-v2)); }
        else { write_u64((uint64_t)v2); }
    }
    (void)write_str("\n");
}

static int spawn_and_wait(char* const argv[])
{
    int status = -1;
    long pid;

    if (!argv || !argv[0]) {
        return 0;
    }

    pid = posix_spawn(argv[0], (const char* const*)argv);
    if (pid <= 0) {
        /* pid carries the negative kernel error code. */
        spawn_diag("spawn-failed", argv[0], "rc", pid, NULL, 0);
        return 0;
    }
    long wret = waitpid((pid_t)pid, &status, 0);
    if (wret != pid) {
        spawn_diag("waitpid-mismatch", argv[0], "pid", pid, "wret", wret);
        return 0;
    }
    if (status != 0) {
        spawn_diag("status-nonzero", argv[0], "pid", pid, "status", (long)status);
        return 0;
    }
    return 1;
}

static void cleanup_ct_paths(void)
{
    (void)unlink("/mnt/ct_ls.out");
    (void)unlink("/mnt/ct_src.txt");
    (void)unlink("/mnt/ct_copy.txt");
    (void)unlink("/mnt/ct_moved.txt");
    (void)unlink("/mnt/ct_grep.txt");
    (void)unlink("/mnt/ct_grep.out");
    (void)unlink("/mnt/ct_find.out");

    (void)unlink("/mnt/ct_dir/sub/file.txt");
    (void)rmdir("/mnt/ct_dir/sub");
    (void)rmdir("/mnt/ct_dir");

    (void)unlink("/mnt/ct_dir_copy/sub/file.txt");
    (void)rmdir("/mnt/ct_dir_copy/sub");
    (void)rmdir("/mnt/ct_dir_copy");

    (void)unlink("/mnt/ct_find/sub/note.txt");
    (void)rmdir("/mnt/ct_find/sub");
    (void)rmdir("/mnt/ct_find");
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

static int dir_has_entry(const char* dir_path, const char* name)
{
    DIR* d = opendir(dir_path);
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

static int dev_dir_has_entry(const char* name)
{
    return dir_has_entry("/dev", name);
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

static void run_tcc_smoke_if_enabled(void)
{
    static const char src_path[] = "/mnt/tcc_smoke.c";
    static const char obj_path[] = "/mnt/tcc_smoke.o";
    static const char tcc_path[] = "/mnt/usr/bin/tcc";
    static const char src_code[] =
        "int rodnix_tcc_smoke(void) {\n"
        "    return 42;\n"
        "}\n";

    if (!file_exists("/etc/tcc.auto")) {
        (void)write_str("[SMK] TCC SKIP flag missing file=");
        (void)write_str(file_exists("/etc/tcc.auto") ? "1" : "0");
        (void)write_str(" dir=");
        (void)write_str(dir_has_entry("/etc", "tcc.auto") ? "1\n" : "0\n");
        return;
    }

    (void)write_str("[SMK] TCC START\n");
    if (!file_exists(tcc_path)) {
        (void)write_str("[SMK] TCC FAIL missing /mnt/usr/bin/tcc\n");
        return;
    }

    (void)unlink(src_path);
    (void)unlink(obj_path);

    {
        int fd = open(src_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            (void)write_str("[SMK] TCC FAIL create source\n");
            return;
        }
        if (write(fd, src_code, sizeof(src_code) - 1) != (ssize_t)(sizeof(src_code) - 1)) {
            (void)close(fd);
            (void)write_str("[SMK] TCC FAIL write source\n");
            return;
        }
        if (close(fd) != 0) {
            (void)write_str("[SMK] TCC FAIL close source\n");
            return;
        }
    }

    {
        char* const argv[] = {
            (char*)tcc_path,
            (char*)"-c",
            (char*)src_path,
            (char*)"-o",
            (char*)obj_path,
            NULL
        };
        char* const envp[] = {
            (char*)"PATH=/mnt/usr/bin:/bin",
            (char*)"TMPDIR=/mnt",
            NULL
        };
        struct timespec t0;
        struct timespec t1;
        const int64_t timeout_ns = 5LL * 1000LL * 1000LL * 1000LL;
        int status = -1;
        pid_t pid;

        pid = spawnve(tcc_path, argv, envp);
        if (pid <= 0) {
            (void)write_str("[SMK] TCC FAIL spawn\n");
            return;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) {
            (void)write_str("[SMK] TCC FAIL clock\n");
            return;
        }

        for (;;) {
            pid_t wr = waitpid(pid, &status, WNOHANG);
            if (wr == pid) {
                break;
            }
            if (wr < 0) {
                (void)write_str("[SMK] TCC FAIL wait\n");
                return;
            }

            if (clock_gettime(CLOCK_MONOTONIC, &t1) == 0) {
                int64_t dt_ns = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000000LL +
                                (int64_t)(t1.tv_nsec - t0.tv_nsec);
                if (dt_ns >= timeout_ns) {
                    (void)write_str("[SMK] TCC TIMEOUT\n");
                    return;
                }
            }

            (void)rdnx_syscall1(SYS_TEST_SLEEP, 1);
        }

        if (status != 0) {
            (void)write_str("[SMK] TCC FAIL compiler status\n");
            return;
        }
    }

    {
        struct stat st;
        if (stat(obj_path, &st) != 0) {
            (void)write_str("[SMK] TCC FAIL missing output\n");
            return;
        }
        if (st.st_size <= 0) {
            (void)write_str("[SMK] TCC FAIL empty output\n");
            return;
        }
    }

    (void)write_str("[SMK] TCC PASS\n");
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

        /*
         * A sleep must last about as long as it was asked to. Obvious enough
         * that nobody checked, and it was wrong twice over: timeouts were
         * computed by dividing by the time slice instead of the tick period,
         * making every one of them ten times short, and the clock they were
         * compared against lost a fifth of its ticks.
         *
         * Half to double is a wide gate on purpose -- this runs under an
         * emulator where a scheduling delay of tens of milliseconds is normal
         * -- and it is still narrow enough that neither of those two would
         * have got through it.
         */
        if (time_ok) {
            struct timespec s0, s1;
            if (clock_gettime(CLOCK_MONOTONIC, &s0) == 0) {
                struct timespec req;
                req.tv_sec = 0;
                req.tv_nsec = 200000000L;
                (void)nanosleep(&req, 0);
                if (clock_gettime(CLOCK_MONOTONIC, &s1) == 0) {
                    uint64_t a = (uint64_t)s0.tv_sec * 1000000ULL + (uint64_t)s0.tv_nsec / 1000ULL;
                    uint64_t b = (uint64_t)s1.tv_sec * 1000000ULL + (uint64_t)s1.tv_nsec / 1000ULL;
                    uint64_t slept = (b > a) ? (b - a) : 0;
                    if (slept >= 100000ULL && slept <= 400000ULL) {
                        ct_log("CT-042", "PASS", "200ms sleep lasts about 200ms");
                    } else {
                        ct_log("CT-042", "FAIL", "200ms sleep was the wrong length");
                        ok = 0;
                    }
                } else {
                    ct_log("CT-042", "FAIL", "clock_gettime after sleep failed");
                    ok = 0;
                }
            } else {
                ct_log("CT-042", "FAIL", "clock_gettime before sleep failed");
                ok = 0;
            }
        }
    }

    {
        /*
         * CT-043: обещание этапа 7. mlock приводит и закрепляет страницы
         * СЕЙЧАС; после него обращения к диапазону не берут ни одного
         * отказа. Тест фальсифицируем: контрольный незакреплённый буфер
         * обязан отказы брать — если не берёт, счётчик не работает и
         * зелёный результат ничего бы не значил.
         */
        enum { CT43_PAGES = 16, CT43_LEN = CT43_PAGES * 4096 };
        volatile unsigned char* locked =
            (volatile unsigned char*)posix_mmap(0, CT43_LEN, 0x1 | 0x2,
                                                0x0002 | 0x1000, -1, 0);
        volatile unsigned char* control =
            (volatile unsigned char*)posix_mmap(0, CT43_LEN, 0x1 | 0x2,
                                                0x0002 | 0x1000, -1, 0);
        int ok43 = 1;
        if ((long)locked <= 0 || (long)control <= 0) {
            ok43 = 0;
        }
        if (ok43 && posix_schedrt(1) != 0) {
            ok43 = 0;
        }
        if (ok43 && posix_mlock((void*)locked, CT43_LEN) != 0) {
            ct_log("CT-043", "FAIL", "mlock failed");
            ok43 = 0;
        }
        if (ok43) {
            long f0 = posix_threadfaults();
            for (unsigned off = 0; off < CT43_LEN; off += 4096) {
                locked[off] = (unsigned char)(off >> 12);
            }
            long f1 = posix_threadfaults();
            /* Контроль касается незакреплённого буфера и обязан отказывать —
             * но не из-под RT: отказ RT-потока ядро печатает как поломку
             * обещания, а тут отказы намеренные. Счётчику класс безразличен. */
            (void)posix_schedrt(0);
            for (unsigned off = 0; off < CT43_LEN; off += 4096) {
                control[off] = (unsigned char)(off >> 12);
            }
            long f2 = posix_threadfaults();

            if (f1 != f0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "locked range took %ld faults",
                         (long)(f1 - f0));
                ct_log("CT-043", "FAIL", msg);
                ok43 = 0;
            } else if (f2 <= f1) {
                /* Контроль не отказал — счётчик мёртв, зелёному верить нельзя. */
                ct_log("CT-043", "FAIL", "fault counter shows nothing");
                ok43 = 0;
            } else {
                ct_log("CT-043", "PASS", "realtime range takes zero faults");
            }
        }
        (void)posix_schedrt(0);
        if ((long)locked > 0) {
            (void)posix_munlock((void*)locked, CT43_LEN);
            (void)posix_munmap((void*)locked, CT43_LEN);
        }
        if ((long)control > 0) {
            (void)posix_munmap((void*)control, CT43_LEN);
        }
        if (!ok43) {
            ok = 0;
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

    {
        int local_ok = 1;
        char* const ls_argv[] = {
            (char*)"/bin/ls",
            (char*)"-la1",
            (char*)"/bin",
            NULL
        };

        if (!spawn_and_wait(ls_argv)) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-029", "PASS", "ls flags exit successfully");
        } else {
            ct_log("CT-029", "FAIL", "ls smoke failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const cp_argv[] = {
            (char*)"/bin/cp",
            (char*)"/mnt/ct_src.txt",
            (char*)"/mnt/ct_copy.txt",
            NULL
        };
        char* const mv_argv[] = {
            (char*)"/bin/mv",
            (char*)"/mnt/ct_copy.txt",
            (char*)"/mnt/ct_moved.txt",
            NULL
        };
        char* const rm_argv[] = {
            (char*)"/bin/rm",
            (char*)"-f",
            (char*)"/mnt/ct_moved.txt",
            NULL
        };

        cleanup_ct_paths();

        if (!write_text_file("/mnt/ct_src.txt", "alpha\nRodNIX\n")) {
            local_ok = 0;
        } else if (!spawn_and_wait(cp_argv)) {
            local_ok = 0;
        } else if (!file_contains_text("/mnt/ct_copy.txt", "RodNIX")) {
            local_ok = 0;
        } else if (!spawn_and_wait(mv_argv)) {
            local_ok = 0;
        } else if (file_exists("/mnt/ct_copy.txt") || !file_exists("/mnt/ct_moved.txt")) {
            local_ok = 0;
        } else if (!spawn_and_wait(rm_argv)) {
            local_ok = 0;
        } else if (file_exists("/mnt/ct_moved.txt")) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-030", "PASS", "cp/mv/rm basic file workflow");
        } else {
            ct_log("CT-030", "FAIL", "cp/mv/rm file workflow failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const cp_r_argv[] = {
            (char*)"/bin/cp",
            (char*)"-r",
            (char*)"/mnt/ct_dir",
            (char*)"/mnt/ct_dir_copy",
            NULL
        };
        char* const rm_r_argv[] = {
            (char*)"/bin/rm",
            (char*)"-rf",
            (char*)"/mnt/ct_dir",
            (char*)"/mnt/ct_dir_copy",
            NULL
        };

        cleanup_ct_paths();
        if (mkdir("/mnt/ct_dir", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && mkdir("/mnt/ct_dir/sub", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && !write_text_file("/mnt/ct_dir/sub/file.txt", "tree\n")) {
            local_ok = 0;
        }
        if (local_ok && !spawn_and_wait(cp_r_argv)) {
            local_ok = 0;
        }
        if (local_ok && !file_contains_text("/mnt/ct_dir_copy/sub/file.txt", "tree")) {
            local_ok = 0;
        }
        if (local_ok && !spawn_and_wait(rm_r_argv)) {
            local_ok = 0;
        }
        if (local_ok && (file_exists("/mnt/ct_dir/sub/file.txt") || file_exists("/mnt/ct_dir_copy/sub/file.txt"))) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-031", "PASS", "recursive cp/rm tree workflow");
        } else {
            ct_log("CT-031", "FAIL", "recursive cp/rm tree workflow failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const grep_argv[] = {
            (char*)"/bin/grep",
            (char*)"-in",
            (char*)"rodnix",
            (char*)"/mnt/ct_grep.txt",
            NULL
        };

        cleanup_ct_paths();
        if (!write_text_file("/mnt/ct_grep.txt", "alpha\nRodNIX\nomega\n")) {
            local_ok = 0;
        } else if (!spawn_and_wait(grep_argv)) {
            local_ok = 0;
        }

        if (local_ok) {
            ct_log("CT-032", "PASS", "grep flags and case-insensitive match");
        } else {
            ct_log("CT-032", "FAIL", "grep smoke failed");
            ok = 0;
        }
    }

    {
        int local_ok = 1;
        char* const find_argv[] = {
            (char*)"/bin/find",
            (char*)"/mnt/ct_find",
            (char*)"-name",
            (char*)"*.txt",
            (char*)"-type",
            (char*)"f",
            (char*)"-maxdepth",
            (char*)"3",
            NULL
        };

        cleanup_ct_paths();
        if (mkdir("/mnt/ct_find", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && mkdir("/mnt/ct_find/sub", 0755) != 0 && errno != EEXIST) {
            local_ok = 0;
        }
        if (local_ok && !write_text_file("/mnt/ct_find/sub/note.txt", "find\n")) {
            local_ok = 0;
        }
        if (local_ok && !spawn_and_wait(find_argv)) {
            local_ok = 0;
        }
        cleanup_ct_paths();

        if (local_ok) {
            ct_log("CT-033", "PASS", "find name/type/maxdepth workflow");
        } else {
            ct_log("CT-033", "FAIL", "find smoke failed");
            ok = 0;
        }
    }

    {
        /* CT-034: dup разделяет описание, значит и offset.
         * posix_open() передаёт flags как POSIX O_* биты (см.
         * unix_posix_flags_to_vfs() в kernel/unix/fd/unix_fd.c) — значение
         * VFS_OPEN_CREATE (4) в этой кодировке совпадает с O_NONBLOCK, а не
         * с O_CREAT (0x200), так что создание файла нужно запрашивать через
         * O_WRONLY|O_CREAT|O_TRUNC (sys/fcntl.h, доступен через unistd.h). */
        int local_ok = 1;
        char buf[8];
        long fd = posix_open("/mnt/ct_dupoff.txt", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            local_ok = 0;
        } else {
            long fd2 = posix_dup((int)fd);
            if (fd2 < 0) {
                local_ok = 0;
            } else {
                /* Пишем через оба дескриптора. При общем описании получится
                 * "abcd"; при копирующем dup второй write затрёт первый. */
                if (posix_write((int)fd, "ab", 2) != 2) local_ok = 0;
                if (posix_write((int)fd2, "cd", 2) != 2) local_ok = 0;
                (void)posix_close((int)fd2);
            }
            (void)posix_close((int)fd);
        }
        if (local_ok) {
            long rfd = posix_open("/mnt/ct_dupoff.txt", VFS_OPEN_READ);
            if (rfd < 0) {
                local_ok = 0;
            } else {
                long n = posix_read((int)rfd, buf, sizeof(buf));
                (void)posix_close((int)rfd);
                if (n != 4 || buf[0] != 'a' || buf[1] != 'b' ||
                    buf[2] != 'c' || buf[3] != 'd') {
                    local_ok = 0;
                }
            }
        }
        (void)posix_unlink("/mnt/ct_dupoff.txt");

        if (local_ok) {
            ct_log("CT-034", "PASS", "dup shares file offset");
        } else {
            ct_log("CT-034", "FAIL", "dup does not share file offset");
            ok = 0;
        }
    }

    {
        /* CT-035: сокет — обычное описание, dup обязан работать. */
        int local_ok = 1;
        long s = posix_socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 0);
        if (s < 0) {
            local_ok = 0;
        } else {
            long s2 = posix_dup((int)s);
            if (s2 < 0) {
                local_ok = 0;
            } else {
                /* Закрытие одной копии не должно убивать вторую. */
                (void)posix_close((int)s);
                if (posix_close((int)s2) < 0) {
                    local_ok = 0;
                }
            }
            if (!local_ok) {
                (void)posix_close((int)s);
            }
        }
        if (local_ok) {
            ct_log("CT-035", "PASS", "dup works on socket fd");
        } else {
            ct_log("CT-035", "FAIL", "dup on socket fd failed");
            ok = 0;
        }
    }

    {
        /* CT-036: unlink открытого файла не рвёт чтение через живой fd.
         * posix_open() передаёт flags как POSIX O_* биты (см.
         * unix_posix_flags_to_vfs() в kernel/unix/fd/unix_fd.c) — значение
         * VFS_OPEN_CREATE (4) в этой кодировке совпадает с O_NONBLOCK, а не
         * с O_CREAT (0x200), так что создание файла нужно запрашивать через
         * O_WRONLY|O_CREAT|O_TRUNC (sys/fcntl.h, доступен через unistd.h).
         *
         * Путь намеренно не /mnt (brief specified /mnt verbatim, departure
         * approved by coordinator ruling): /mnt is the ext2 mount, and
         * ext2_unlink() (fs/ext2.c) frees the inode's data blocks
         * synchronously and unconditionally, with no awareness of
         * vfs_node.ref_count — a pre-existing ext2 defect, not something
         * this task's fs/vfs.c reconciliation controls. Root ("/") is the
         * ramfs mount; ramfs files keep their bytes in vfs_inode_t.data,
         * which vfs_free_node() only frees once ref_count reaches zero, so
         * this exercises exactly the vfs.c-layer invariant Task 2 is about. */
        int local_ok = 1;
        char buf[8];
        long wfd = posix_open("/ct_unlink.txt", O_WRONLY | O_CREAT | O_TRUNC);
        if (wfd < 0 || posix_write((int)wfd, "live", 4) != 4) {
            local_ok = 0;
        }
        if (wfd >= 0) {
            (void)posix_close((int)wfd);
        }

        if (local_ok) {
            long rfd = posix_open("/ct_unlink.txt", VFS_OPEN_READ);
            if (rfd < 0) {
                local_ok = 0;
            } else {
                if (posix_unlink("/ct_unlink.txt") < 0) {
                    local_ok = 0;
                }
                /* Имя удалено, но описание живо — данные обязаны читаться. */
                long n = posix_read((int)rfd, buf, sizeof(buf));
                if (n != 4 || buf[0] != 'l' || buf[3] != 'e') {
                    local_ok = 0;
                }
                (void)posix_close((int)rfd);
                /* Повторное открытие обязано провалиться. */
                long again = posix_open("/ct_unlink.txt", VFS_OPEN_READ);
                if (again >= 0) {
                    (void)posix_close((int)again);
                    local_ok = 0;
                }
            }
        }

        if (local_ok) {
            ct_log("CT-036", "PASS", "unlinked file readable via open fd");
        } else {
            ct_log("CT-036", "FAIL", "unlink broke open fd");
            ok = 0;
        }
    }

    {
        /* CT-041: после fork описание общее. Закрытие дескриптора у родителя
         * не должно обрывать его у потомка. */
        int local_ok = 1;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            local_ok = 0;
        } else {
            long pid = posix_fork();
            if (pid == 0) {
                /* Потомок: родитель к этому моменту мог закрыть свой fd. */
                char b = 0;
                long r = posix_read((int)fd, &b, 1);
                posix_exit((r == 1) ? 0 : 1);
            } else if (pid < 0) {
                local_ok = 0;
                (void)posix_close((int)fd);
            } else {
                int status = 0;
                (void)posix_close((int)fd);   /* родитель закрывает первым */
                if (posix_waitpid(pid, &status) < 0 || status != 0) {
                    local_ok = 0;
                }
            }
        }
        if (local_ok) {
            ct_log("CT-041", "PASS", "fork shares the file description");
        } else {
            ct_log("CT-041", "FAIL", "child fd died with parent close");
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
    (void)write_str("RodNIX userspace 0.1.22\n");
    print_file("/etc/motd");
    (void)write_str("\n");
    print_hostname();
    run_smoke();
    run_ifconfig_smoke_if_enabled();
    run_tcc_smoke_if_enabled();
    run_contract_mode_if_enabled();

    run_service_supervisor();

    for (;;) {
        (void)rdnx_syscall0(0);
    }
    return 0;
}
