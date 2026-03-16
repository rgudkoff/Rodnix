/*
 * ls — list directory contents
 * Flags: -a (all), -l (long), -1 (one per line), -h (human sizes)
 */
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>

static int opt_all   = 0; /* -a */
static int opt_long  = 0; /* -l */
static int opt_one   = 0; /* -1 */
static int opt_human = 0; /* -h */
static int use_color = 0;

/* ── helpers ─────────────────────────────────────────────── */

static void mode_str(mode_t m, char out[11])
{
    out[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : '-';
    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    out[3] = (m & S_IXUSR) ? 'x' : '-';
    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    out[6] = (m & S_IXGRP) ? 'x' : '-';
    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    out[9] = (m & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

static void human_size(long long sz, char out[16])
{
    if (sz < 1024LL) {
        snprintf(out, 16, "%lldB", sz);
    } else if (sz < 1024LL * 1024) {
        snprintf(out, 16, "%lldK", sz / 1024);
    } else if (sz < 1024LL * 1024 * 1024) {
        snprintf(out, 16, "%lldM", sz / (1024 * 1024));
    } else {
        snprintf(out, 16, "%lldG", sz / (1024 * 1024 * 1024));
    }
}

static void print_entry(const char *dir, const char *name, uint8_t dtype)
{
    /* skip hidden unless -a */
    if (!opt_all && name[0] == '.') return;

    if (opt_long) {
        /* build full path for stat */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        struct stat st;
        memset(&st, 0, sizeof(st));
        lstat(path, &st);

        char mbuf[11];
        mode_str(st.st_mode, mbuf);

        char sbuf[16];
        if (opt_human) {
            human_size((long long)st.st_size, sbuf);
        } else {
            snprintf(sbuf, sizeof(sbuf), "%lld", (long long)st.st_size);
        }

        if (use_color) {
            if (S_ISDIR(st.st_mode))
                printf("%s %8s \033[1;34m%s\033[0m/\n", mbuf, sbuf, name);
            else if (st.st_mode & S_IXUSR)
                printf("%s %8s \033[1;32m%s\033[0m\n", mbuf, sbuf, name);
            else
                printf("%s %8s %s\n", mbuf, sbuf, name);
        } else {
            const char *suf = S_ISDIR(st.st_mode) ? "/" : "";
            printf("%s %8s %s%s\n", mbuf, sbuf, name, suf);
        }
    } else {
        /* short listing */
        if (use_color) {
            if (dtype == DT_DIR)
                printf("\033[1;34m%s\033[0m/", name);
            else
                printf("%s", name);
        } else {
            printf("%s%s", name, dtype == DT_DIR ? "/" : "");
        }

        if (opt_one) {
            putchar('\n');
        } else {
            putchar(' ');
        }
    }
}

static int ls_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        print_entry(path, de->d_name, de->d_type);
    }
    closedir(d);

    if (!opt_long && !opt_one) putchar('\n');
    return 0;
}

int main(int argc, char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "al1h")) != -1) {
        switch (c) {
        case 'a': opt_all   = 1; break;
        case 'l': opt_long  = 1; break;
        case '1': opt_one   = 1; break;
        case 'h': opt_human = 1; break;
        default:
            fprintf(stderr, "usage: ls [-al1h] [path...]\n");
            return 1;
        }
    }

    use_color = isatty(STDOUT_FILENO);

    if (optind >= argc) {
        return ls_dir(".");
    }

    int ret = 0;
    for (int i = optind; i < argc; i++) {
        if (argc - optind > 1) printf("%s:\n", argv[i]);
        ret |= ls_dir(argv[i]);
    }
    return ret;
}
