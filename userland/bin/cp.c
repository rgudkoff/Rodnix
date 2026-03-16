/*
 * cp — copy files and directories
 * Usage: cp [-r] src dst
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

static int opt_recursive = 0;

#define BUFSZ 8192

static int copy_file(const char *src, const char *dst)
{
    int fdin = open(src, O_RDONLY);
    if (fdin < 0) {
        fprintf(stderr, "cp: %s: %s\n", src, strerror(errno));
        return 1;
    }
    int fdout = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fdout < 0) {
        fprintf(stderr, "cp: %s: %s\n", dst, strerror(errno));
        close(fdin);
        return 1;
    }

    char buf[BUFSZ];
    int ret = 0;
    for (;;) {
        ssize_t n = read(fdin, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            fprintf(stderr, "cp: read %s: %s\n", src, strerror(errno));
            ret = 1;
            break;
        }
        ssize_t w = 0;
        while (w < n) {
            ssize_t r = write(fdout, buf + w, (size_t)(n - w));
            if (r < 0) {
                fprintf(stderr, "cp: write %s: %s\n", dst, strerror(errno));
                ret = 1;
                goto done;
            }
            w += r;
        }
    }
done:
    close(fdin);
    close(fdout);
    return ret;
}

static int copy_recursive(const char *src, const char *dst);

static int copy_recursive(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) < 0) {
        fprintf(stderr, "cp: %s: %s\n", src, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, 0755) < 0 && errno != EEXIST) {
            fprintf(stderr, "cp: mkdir %s: %s\n", dst, strerror(errno));
            return 1;
        }
        DIR *d = opendir(src);
        if (!d) {
            fprintf(stderr, "cp: %s: %s\n", src, strerror(errno));
            return 1;
        }
        int ret = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            char nsrc[512], ndst[512];
            snprintf(nsrc, sizeof(nsrc), "%s/%s", src, de->d_name);
            snprintf(ndst, sizeof(ndst), "%s/%s", dst, de->d_name);
            ret |= copy_recursive(nsrc, ndst);
        }
        closedir(d);
        return ret;
    }

    return copy_file(src, dst);
}

int main(int argc, char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "r")) != -1) {
        switch (c) {
        case 'r': opt_recursive = 1; break;
        default:
            fprintf(stderr, "usage: cp [-r] src dst\n");
            return 1;
        }
    }

    int remaining = argc - optind;
    if (remaining < 2) {
        fprintf(stderr, "usage: cp [-r] src dst\n");
        return 1;
    }

    const char *src = argv[optind];
    const char *dst = argv[optind + 1];

    struct stat st;
    if (lstat(src, &st) < 0) {
        fprintf(stderr, "cp: %s: %s\n", src, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!opt_recursive) {
            fprintf(stderr, "cp: %s is a directory (use -r)\n", src);
            return 1;
        }
        return copy_recursive(src, dst);
    }

    /* dst may be an existing directory — append basename */
    char real_dst[512];
    struct stat dst_st;
    if (lstat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        snprintf(real_dst, sizeof(real_dst), "%s/%s", dst, base);
        dst = real_dst;
    }

    return copy_file(src, dst);
}
