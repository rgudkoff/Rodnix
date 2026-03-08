/*
 * ls.c
 * Minimal external ls utility scaffold.
 */

#include <unistd.h>
#include "dirent.h"

static unsigned long cstr_len(const char* s)
{
    unsigned long n = 0;
    if (!s) {
        return 0;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void put_str(const char* s)
{
    (void)write(STDOUT_FILENO, s, cstr_len(s));
}

int main(int argc, char** argv)
{
    const char* path = "/";
    DIR* d;
    struct dirent* de;

    if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
        path = argv[1];
    }

    d = opendir(path);
    if (!d) {
        put_str("ls: opendir failed\n");
        return 1;
    }
    while ((de = readdir(d)) != 0) {
        put_str(de->d_name);
        if (de->d_type == DT_DIR) {
            put_str("/");
        }
        put_str("\n");
    }
    (void)closedir(d);
    return 0;
}
