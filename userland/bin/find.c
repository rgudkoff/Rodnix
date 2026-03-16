/*
 * find — search for files in a directory tree
 * Usage: find [path] [-name pat] [-type f|d] [-maxdepth n] [-print]
 *
 * Pattern matching: supports '*' (any chars) and '?' (any single char).
 */
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* simple wildcard match: pat vs str */
static int wildmatch(const char *pat, const char *str)
{
    for (;;) {
        if (*pat == '\0') return *str == '\0';
        if (*pat == '*') {
            pat++;
            do {
                if (wildmatch(pat, str)) return 1;
            } while (*str++ != '\0');
            return 0;
        }
        if (*pat != '?' && *pat != *str) return 0;
        pat++;
        str++;
    }
}

/* ── predicates ─────────────────────────────────────────── */
static const char *pred_name     = NULL; /* -name pat */
static int         pred_type     = 0;    /* 'f'=file, 'd'=dir, 0=any */
static int         pred_maxdepth = -1;   /* -1 = unlimited */
static int         found_any     = 0;

static int matches(const char *name, const struct stat *st)
{
    if (pred_name && !wildmatch(pred_name, name)) return 0;
    if (pred_type) {
        if (pred_type == 'f' && !S_ISREG(st->st_mode)) return 0;
        if (pred_type == 'd' && !S_ISDIR(st->st_mode)) return 0;
    }
    return 1;
}

static void do_find(const char *path, const char *name, int depth)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        fprintf(stderr, "find: %s: %s\n", path, strerror(errno));
        return;
    }

    if (matches(name, &st)) {
        puts(path);
        found_any = 1;
    }

    if (!S_ISDIR(st.st_mode)) return;
    if (pred_maxdepth >= 0 && depth >= pred_maxdepth) return;

    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "find: %s: %s\n", path, strerror(errno));
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        do_find(child, de->d_name, depth + 1);
    }
    closedir(d);
}

int main(int argc, char **argv)
{
    /* find doesn't use getopt: it has its own argument syntax */
    const char *startpath = ".";
    int i = 1;

    /* first non-option argument that doesn't start with '-' is path */
    if (i < argc && argv[i][0] != '-') {
        startpath = argv[i++];
    }

    while (i < argc) {
        if (strcmp(argv[i], "-name") == 0) {
            if (++i >= argc) { fprintf(stderr, "find: -name needs argument\n"); return 1; }
            pred_name = argv[i++];
        } else if (strcmp(argv[i], "-type") == 0) {
            if (++i >= argc) { fprintf(stderr, "find: -type needs argument\n"); return 1; }
            if (argv[i][0] == 'f' || argv[i][0] == 'd') {
                pred_type = argv[i][0];
            } else {
                fprintf(stderr, "find: -type: unknown type '%s'\n", argv[i]);
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "-maxdepth") == 0) {
            if (++i >= argc) { fprintf(stderr, "find: -maxdepth needs argument\n"); return 1; }
            pred_maxdepth = atoi(argv[i++]);
        } else if (strcmp(argv[i], "-print") == 0) {
            i++; /* -print is default, just skip */
        } else {
            fprintf(stderr, "find: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    /* strip trailing slash from startpath for clean output */
    char start[512];
    strncpy(start, startpath, sizeof(start) - 1);
    start[sizeof(start) - 1] = '\0';
    size_t slen = strlen(start);
    while (slen > 1 && start[slen - 1] == '/') start[--slen] = '\0';

    const char *base = strrchr(start, '/');
    base = base ? base + 1 : start;

    do_find(start, base, 0);
    return 0;
}
