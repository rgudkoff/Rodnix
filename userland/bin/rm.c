/*
 * rm — remove files and directories
 * Usage: rm [-rf] path...
 */
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

static int opt_recursive = 0;
static int opt_force     = 0;

static int rm_recursive(const char *path);

static int rm_recursive(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        if (opt_force && errno == ENOENT) return 0;
        fprintf(stderr, "rm: %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!opt_recursive) {
            fprintf(stderr, "rm: %s: is a directory\n", path);
            return 1;
        }
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "rm: %s: %s\n", path, strerror(errno));
            return 1;
        }
        int ret = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            char child[512];
            snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
            ret |= rm_recursive(child);
        }
        closedir(d);
        if (ret) return ret;
        if (rmdir(path) < 0) {
            fprintf(stderr, "rm: rmdir %s: %s\n", path, strerror(errno));
            return 1;
        }
        return 0;
    }

    if (unlink(path) < 0) {
        if (opt_force && errno == ENOENT) return 0;
        fprintf(stderr, "rm: %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "rf")) != -1) {
        switch (c) {
        case 'r': opt_recursive = 1; break;
        case 'f': opt_force     = 1; break;
        default:
            fprintf(stderr, "usage: rm [-rf] path...\n");
            return 1;
        }
    }

    if (optind >= argc) {
        if (opt_force) return 0;
        fprintf(stderr, "usage: rm [-rf] path...\n");
        return 1;
    }

    int ret = 0;
    for (int i = optind; i < argc; i++) {
        ret |= rm_recursive(argv[i]);
    }
    return ret;
}
