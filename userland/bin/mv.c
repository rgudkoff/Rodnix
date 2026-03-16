/*
 * mv — move (rename) files
 * Usage: mv src dst
 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define BUFSZ 8192

static int copy_file_and_unlink(const char *src, const char *dst)
{
    int fdin = open(src, O_RDONLY);
    if (fdin < 0) {
        fprintf(stderr, "mv: %s: %s\n", src, strerror(errno));
        return 1;
    }
    int fdout = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fdout < 0) {
        fprintf(stderr, "mv: %s: %s\n", dst, strerror(errno));
        close(fdin);
        return 1;
    }

    char buf[BUFSZ];
    int ret = 0;
    for (;;) {
        ssize_t n = read(fdin, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) { ret = 1; break; }
        ssize_t w = 0;
        while (w < n) {
            ssize_t r = write(fdout, buf + w, (size_t)(n - w));
            if (r < 0) { ret = 1; goto done; }
            w += r;
        }
    }
done:
    close(fdin);
    close(fdout);
    if (ret == 0) {
        if (unlink(src) < 0) {
            fprintf(stderr, "mv: unlink %s: %s\n", src, strerror(errno));
            ret = 1;
        }
    } else {
        unlink(dst);
    }
    return ret;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: mv src dst\n");
        return 1;
    }

    const char *src = argv[1];
    const char *dst = argv[2];

    /* resolve dst if it's a directory */
    char real_dst[512];
    struct stat dst_st;
    if (lstat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        const char *base = strrchr(src, '/');
        base = base ? base + 1 : src;
        snprintf(real_dst, sizeof(real_dst), "%s/%s", dst, base);
        dst = real_dst;
    }

    /* try atomic rename first */
    if (rename(src, dst) == 0) return 0;

    /* rename failed — fall back to copy + unlink (handles same-FS errors too) */
    return copy_file_and_unlink(src, dst);
}
