/*
 * top — display and update sorted information about processes
 *
 * Refreshes every second. Press 'q' to quit.
 * Uses /proc/status, /proc/meminfo, /proc/uptime.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>

#define MAX_PROCS  512
#define BUF_CAP   (MAX_PROCS * 128)
#define MEMINFO_CAP 512
#define UPTIME_CAP  64

typedef struct {
    long   pid;
    long   ppid;
    char   stat;
    long   nthrd;
    long   uid;
    long   cputicks;
    char   command[64];
} top_proc_t;

static struct termios g_orig_termios;
static int g_raw_mode = 0;

static void restore_terminal(void)
{
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_raw_mode = 0;
    }
}

static void sig_handler(int sig)
{
    (void)sig;
    restore_terminal();
    /* clear screen, move to top-left */
    write(STDOUT_FILENO, "\033[2J\033[H", 7);
    _exit(0);
}

static int enable_raw_mode(void)
{
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) {
        return -1;
    }
    struct termios raw = g_orig_termios;
    raw.c_lflag &= (unsigned)~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return -1;
    }
    g_raw_mode = 1;
    return 0;
}

static int read_all(const char* path, char* buf, int cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int total = 0, n;
    while (total < cap - 1) {
        n = (int)read(fd, buf + total, (size_t)(cap - 1 - total));
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    close(fd);
    return total;
}

static long parse_long(const char* s)
{
    long v = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

/* Parse /proc/meminfo: extract MemTotal and MemFree in kB */
static void parse_meminfo(const char* buf, long* total_kb, long* free_kb)
{
    *total_kb = 0;
    *free_kb  = 0;
    const char* p = buf;
    while (*p) {
        if (strncmp(p, "MemTotal:", 9) == 0) {
            p += 9;
            *total_kb = parse_long(p);
        } else if (strncmp(p, "MemFree:", 8) == 0) {
            p += 8;
            *free_kb = parse_long(p);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

/* Parse /proc/uptime: returns total seconds (integer part) */
static long parse_uptime(const char* buf)
{
    return parse_long(buf);
}

static int parse_proc_status(char* buf, top_proc_t* procs, int max)
{
    int count = 0;
    char* p = buf;
    while (*p && count < max) {
        /* skip header */
        if (*p == 'P' || *p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        char* line = p;
        while (*p && *p != '\n') p++;
        char* line_end = p;
        if (*p == '\n') p++;
        *line_end = '\0';

        char* f[8];
        int nf = 0;
        char* tok = line;
        f[nf++] = tok;
        for (char* c = line; *c && nf < 8; c++) {
            if (*c == '\t') { *c = '\0'; f[nf++] = c + 1; }
        }
        if (nf < 7) continue;

        top_proc_t* pr = &procs[count++];
        pr->pid      = parse_long(f[0]);
        pr->ppid     = parse_long(f[1]);
        pr->stat     = f[2][0];
        pr->nthrd    = parse_long(f[3]);
        pr->uid      = parse_long(f[4]);
        pr->cputicks = parse_long(f[5]);
        strncpy(pr->command, f[6], sizeof(pr->command) - 1);
        pr->command[sizeof(pr->command) - 1] = '\0';
    }
    return count;
}

static int cmp_by_pid(const void* a, const void* b)
{
    const top_proc_t* pa = (const top_proc_t*)a;
    const top_proc_t* pb = (const top_proc_t*)b;
    if (pa->pid < pb->pid) return -1;
    if (pa->pid > pb->pid) return  1;
    return 0;
}

static int terminal_rows(void)
{
    struct winsize ws;
    if (isatty(STDOUT_FILENO) &&
        ioctl(STDOUT_FILENO, RDNX_TTY_IOCTL_GETWINSZ, &ws) == 0 &&
        ws.ws_row >= 5) {
        return (int)ws.ws_row;
    }
    return 24;
}

static void format_uptime(long secs, char* buf, int cap)
{
    long h = secs / 3600;
    long m = (secs % 3600) / 60;
    long s = secs % 60;
    /* simple manual formatting */
    int pos = 0;
    if (h > 0) {
        /* write h */
        char tmp[32]; int ti = 30; tmp[31] = '\0';
        long v = h;
        if (v == 0) { tmp[ti--] = '0'; }
        else while (v && ti > 0) { tmp[ti--] = '0' + (int)(v % 10); v /= 10; }
        const char* ts = tmp + ti + 1;
        while (*ts && pos < cap - 1) buf[pos++] = *ts++;
        if (pos < cap - 1) buf[pos++] = 'h';
        if (pos < cap - 1) buf[pos++] = ' ';
    }
    /* write m */
    if (m < 10 && pos < cap - 1) buf[pos++] = '0';
    { long v = m; char tmp[8]; int ti = 6; tmp[7] = '\0';
      if (v == 0) { tmp[ti--] = '0'; }
      else while (v && ti > 0) { tmp[ti--] = '0' + (int)(v % 10); v /= 10; }
      const char* ts = tmp + ti + 1;
      while (*ts && pos < cap - 1) buf[pos++] = *ts++; }
    if (pos < cap - 1) buf[pos++] = ':';
    /* write s */
    if (s < 10 && pos < cap - 1) buf[pos++] = '0';
    { long v = s; char tmp[8]; int ti = 6; tmp[7] = '\0';
      if (v == 0) { tmp[ti--] = '0'; }
      else while (v && ti > 0) { tmp[ti--] = '0' + (int)(v % 10); v /= 10; }
      const char* ts = tmp + ti + 1;
      while (*ts && pos < cap - 1) buf[pos++] = *ts++; }
    buf[pos] = '\0';
}

static void render(const char* uptime_buf, const char* meminfo_buf,
                   char* status_buf, int rows)
{
    long total_kb = 0, free_kb = 0;
    parse_meminfo(meminfo_buf, &total_kb, &free_kb);
    long used_kb = total_kb - free_kb;

    long uptime_s = parse_uptime(uptime_buf);
    char uptime_str[32];
    format_uptime(uptime_s, uptime_str, sizeof(uptime_str));

    top_proc_t* procs = malloc(MAX_PROCS * sizeof(top_proc_t));
    if (!procs) return;
    int count = parse_proc_status(status_buf, procs, MAX_PROCS);
    qsort(procs, (size_t)count, sizeof(top_proc_t), cmp_by_pid);

    int running = 0, sleeping = 0, other = 0;
    for (int i = 0; i < count; i++) {
        if (procs[i].stat == 'R') running++;
        else if (procs[i].stat == 'S') sleeping++;
        else other++;
    }

    /* clear screen, move to top-left */
    fputs("\033[2J\033[H", stdout);

    /* header */
    printf("RodNIX top - up %s\n", uptime_str);
    printf("Tasks: %3d total, %3d running, %3d sleeping, %3d stopped\n",
           count, running, sleeping, other);
    printf("MiB Mem: %5ld total, %5ld free, %5ld used\n",
           total_kb / 1024, free_kb / 1024, used_kb / 1024);
    printf("\n");

    /* column header */
    printf("%6s %6s %c  %5s %10s  %s\n",
           "PID", "PPID", 'S', "THRD", "CPUTICKS", "COMMAND");
    printf("------+------+--+-----+----------+-----------------------\n");

    /* process rows (leave 6 lines for header) */
    int max_rows = rows - 7;
    if (max_rows < 1) max_rows = 1;
    for (int i = 0; i < count && i < max_rows; i++) {
        printf("%6ld %6ld %c  %5ld %10ld  %s\n",
               procs[i].pid, procs[i].ppid, procs[i].stat,
               procs[i].nthrd, procs[i].cputicks, procs[i].command);
    }

    fflush(stdout);
    free(procs);
}

int main(int argc, char* argv[])
{
    int delay_s = 1;
    int max_iter = 0; /* 0 = infinite */

    int c;
    while ((c = getopt(argc, argv, "d:n:")) != -1) {
        switch (c) {
            case 'd': delay_s  = (int)parse_long(optarg); break;
            case 'n': max_iter = (int)parse_long(optarg); break;
            default:
                fprintf(stderr, "usage: top [-d delay] [-n iterations]\n");
                return 1;
        }
    }
    if (delay_s < 1) delay_s = 1;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int is_tty = isatty(STDIN_FILENO);
    if (is_tty) {
        enable_raw_mode();
    }

    char* uptime_buf  = malloc(UPTIME_CAP);
    char* meminfo_buf = malloc(MEMINFO_CAP);
    char* status_buf  = malloc(BUF_CAP);
    if (!uptime_buf || !meminfo_buf || !status_buf) {
        fprintf(stderr, "top: out of memory\n");
        restore_terminal();
        return 1;
    }

    for (int iter = 0; max_iter == 0 || iter < max_iter; iter++) {
        read_all("/proc/uptime",  uptime_buf,  UPTIME_CAP);
        read_all("/proc/meminfo", meminfo_buf, MEMINFO_CAP);
        read_all("/proc/status",  status_buf,  BUF_CAP);

        int rows = terminal_rows();
        render(uptime_buf, meminfo_buf, status_buf, rows);

        /* Wait up to delay_s seconds; quit if 'q' or 'Q' pressed */
        if (is_tty) {
            struct timeval tv;
            tv.tv_sec  = delay_s;
            tv.tv_usec = 0;
            fd_set rds;
            FD_ZERO(&rds);
            FD_SET(STDIN_FILENO, &rds);
            int ret = select(STDIN_FILENO + 1, &rds, NULL, NULL, &tv);
            if (ret > 0) {
                char key;
                if (read(STDIN_FILENO, &key, 1) == 1 &&
                    (key == 'q' || key == 'Q')) {
                    break;
                }
            }
        } else {
            struct timespec ts;
            ts.tv_sec  = delay_s;
            ts.tv_nsec = 0;
            nanosleep(&ts, NULL);
        }
    }

    restore_terminal();
    /* clear screen on exit */
    fputs("\033[2J\033[H", stdout);
    fflush(stdout);

    free(uptime_buf);
    free(meminfo_buf);
    free(status_buf);
    return 0;
}
