/*
 * ps — report process status
 * Reads /proc/status (tab-separated: PID PPID STAT NTHRD UID CPUTICKS COMMAND)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>

#define MAX_PROCS  512
#define BUF_CAP   (MAX_PROCS * 128)

typedef struct {
    long   pid;
    long   ppid;
    char   stat;
    long   nthrd;
    long   uid;
    long   cputicks;
    char   command[128];
} proc_t;

static int opt_all  = 0; /* -a */
static int opt_long = 0; /* -l — show ppid, threads, cputicks */
static int opt_user = 0; /* -u — show username instead of uid */

static long parse_long(const char* s)
{
    long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

static int read_all(int fd, char* buf, int cap)
{
    int total = 0, n;
    while (total < cap - 1) {
        n = (int)read(fd, buf + total, (size_t)(cap - 1 - total));
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    return total;
}

/* Parse tab-separated /proc/status into procs[].
 * Returns number of processes parsed. */
static int parse_proc_status(char* buf, proc_t* procs, int max)
{
    int count = 0;
    char* p = buf;

    while (*p && count < max) {
        /* skip header line(s) */
        if (*p == 'P' || *p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* find end of line */
        char* line = p;
        while (*p && *p != '\n') p++;
        char* line_end = p;
        if (*p == '\n') p++;

        /* null-terminate the line */
        *line_end = '\0';

        /* split by tab: PID PPID STAT NTHRD UID CPUTICKS COMMAND */
        char* f[8];
        int nf = 0;
        char* tok = line;
        f[nf++] = tok;
        for (char* c = line; *c && nf < 8; c++) {
            if (*c == '\t') {
                *c = '\0';
                f[nf++] = c + 1;
            }
        }
        if (nf < 7) continue;

        proc_t* pr = &procs[count++];
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

static void print_header(void)
{
    if (opt_long) {
        if (opt_user)
            printf("%-8s %6s %6s %s %5s %10s  %s\n",
                   "USER", "PID", "PPID", "S", "THRD", "CPUTICKS", "COMMAND");
        else
            printf("%6s %6s %s %5s %10s  %s\n",
                   "PID", "PPID", "S", "THRD", "CPUTICKS", "COMMAND");
    } else {
        if (opt_user)
            printf("%-8s %6s %s  %s\n", "USER", "PID", "S", "COMMAND");
        else
            printf("%6s %s  %s\n", "PID", "S", "COMMAND");
    }
}

static void print_proc(const proc_t* pr)
{
    if (opt_long) {
        if (opt_user) {
            struct passwd* pw = getpwuid((uid_t)pr->uid);
            if (pw)
                printf("%-8s %6ld %6ld %c %5ld %10ld  %s\n",
                       pw->pw_name, pr->pid, pr->ppid, pr->stat,
                       pr->nthrd, pr->cputicks, pr->command);
            else
                printf("%-8ld %6ld %6ld %c %5ld %10ld  %s\n",
                       pr->uid, pr->pid, pr->ppid, pr->stat,
                       pr->nthrd, pr->cputicks, pr->command);
        } else {
            printf("%6ld %6ld %c %5ld %10ld  %s\n",
                   pr->pid, pr->ppid, pr->stat,
                   pr->nthrd, pr->cputicks, pr->command);
        }
    } else {
        if (opt_user) {
            struct passwd* pw = getpwuid((uid_t)pr->uid);
            if (pw)
                printf("%-8s %6ld %c  %s\n", pw->pw_name, pr->pid, pr->stat, pr->command);
            else
                printf("%-8ld %6ld %c  %s\n", pr->uid, pr->pid, pr->stat, pr->command);
        } else {
            printf("%6ld %c  %s\n", pr->pid, pr->stat, pr->command);
        }
    }
}

int main(int argc, char* argv[])
{
    int c;
    while ((c = getopt(argc, argv, "aluf")) != -1) {
        switch (c) {
            case 'a': opt_all  = 1; break;
            case 'l': opt_long = 1; break;
            case 'u': opt_user = 1; break;
            case 'f': opt_long = 1; opt_user = 1; break;
            default:
                fprintf(stderr, "usage: ps [-aluf]\n");
                return 1;
        }
    }

    int fd = open("/proc/status", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ps: cannot open /proc/status: %s\n", strerror(errno));
        return 1;
    }

    char* buf = malloc(BUF_CAP);
    if (!buf) {
        fprintf(stderr, "ps: out of memory\n");
        close(fd);
        return 1;
    }

    read_all(fd, buf, BUF_CAP);
    close(fd);

    proc_t* procs = malloc(MAX_PROCS * sizeof(proc_t));
    if (!procs) {
        fprintf(stderr, "ps: out of memory\n");
        free(buf);
        return 1;
    }

    int count = parse_proc_status(buf, procs, MAX_PROCS);
    free(buf);

    print_header();
    for (int i = 0; i < count; i++) {
        print_proc(&procs[i]);
    }

    free(procs);
    return 0;
}
