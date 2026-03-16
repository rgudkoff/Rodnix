/*
 * grep — search for pattern in files (strstr-based, no regex)
 * Usage: grep [-rlinvci] pattern [file...]
 *   -r  recursive
 *   -l  list filenames only
 *   -i  case-insensitive
 *   -n  print line numbers
 *   -v  invert match
 *   -c  count matching lines
 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <ctype.h>

static int opt_recursive   = 0;
static int opt_list        = 0;
static int opt_nocase      = 0;
static int opt_number      = 0;
static int opt_invert      = 0;
static int opt_count       = 0;
static int opt_color       = 0;
static int show_filename   = 0; /* set when >1 file or -r */
static int found_any       = 0;

static const char *pattern;
static size_t      plen;
static char        pat_lower[1024];

/* case-insensitive strstr */
static const char *istrstr(const char *hay, const char *needle, size_t nlen)
{
    if (nlen == 0) return hay;
    size_t hlen = strlen(hay);
    for (size_t i = 0; i + nlen <= hlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nlen) return hay + i;
    }
    return NULL;
}

static int line_matches(const char *line)
{
    int m;
    if (opt_nocase) {
        m = (istrstr(line, pat_lower, plen) != NULL);
    } else {
        m = (strstr(line, pattern) != NULL);
    }
    return opt_invert ? !m : m;
}

static int grep_fd(int fd, const char *fname)
{
    char buf[4096];
    char line[4096];
    int  lpos = 0;
    long lineno = 0;
    long match_count = 0;
    int  ret = 1; /* no match yet */
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char ch = buf[i];
            if (lpos < (int)sizeof(line) - 1) {
                line[lpos++] = ch;
            }
            if (ch == '\n' || i == n - 1) {
                line[lpos] = '\0';
                lineno++;

                if (line_matches(line)) {
                    ret = 0;
                    found_any = 1;
                    match_count++;

                    if (opt_list) {
                        printf("%s\n", fname);
                        return 0; /* found, stop scanning */
                    }
                    if (!opt_count) {
                        if (show_filename) {
                            if (opt_color)
                                printf("\033[35m%s\033[0m:", fname);
                            else
                                printf("%s:", fname);
                        }
                        if (opt_number) {
                            printf("%ld:", lineno);
                        }
                        /* strip trailing newline for clean output */
                        int l = lpos;
                        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
                            l--;
                        line[l] = '\0';
                        printf("%s\n", line);
                    }
                }
                lpos = 0;
            }
        }
    }

    if (opt_count) {
        if (show_filename)
            printf("%s:", fname);
        printf("%ld\n", match_count);
        if (match_count > 0) ret = 0;
    }

    return ret;
}

static int grep_file(const char *path);
static int grep_dir(const char *path);

static int grep_file(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        fprintf(stderr, "grep: %s: %s\n", path, strerror(errno));
        return 2;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!opt_recursive) {
            fprintf(stderr, "grep: %s: is a directory\n", path);
            return 2;
        }
        return grep_dir(path);
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "grep: %s: %s\n", path, strerror(errno));
        return 2;
    }
    int r = grep_fd(fd, path);
    close(fd);
    return r;
}

static int grep_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "grep: %s: %s\n", path, strerror(errno));
        return 2;
    }
    int ret = 1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        int r = grep_file(child);
        if (r == 0) ret = 0;
        else if (r == 2 && ret == 1) ret = 2;
    }
    closedir(d);
    return ret;
}

int main(int argc, char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "rlinvc")) != -1) {
        switch (c) {
        case 'r': opt_recursive = 1; break;
        case 'l': opt_list      = 1; break;
        case 'i': opt_nocase    = 1; break;
        case 'n': opt_number    = 1; break;
        case 'v': opt_invert    = 1; break;
        case 'c': opt_count     = 1; break;
        default:
            fprintf(stderr, "usage: grep [-rlinvc] pattern [file...]\n");
            return 2;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "usage: grep [-rlinvc] pattern [file...]\n");
        return 2;
    }

    pattern = argv[optind++];
    plen    = strlen(pattern);
    if (opt_nocase) {
        size_t i;
        for (i = 0; i < plen && i < sizeof(pat_lower) - 1; i++)
            pat_lower[i] = (char)tolower((unsigned char)pattern[i]);
        pat_lower[i] = '\0';
    }

    opt_color = isatty(STDOUT_FILENO);

    int nfiles = argc - optind;
    show_filename = (nfiles > 1 || opt_recursive);

    int ret = 1;
    if (optind >= argc) {
        /* read stdin */
        ret = grep_fd(STDIN_FILENO, "(stdin)");
    } else {
        for (int i = optind; i < argc; i++) {
            int r = grep_file(argv[i]);
            if (r == 0) ret = 0;
            else if (r == 2 && ret != 0) ret = 2;
        }
    }

    return ret;
}
