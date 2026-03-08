/*
 * ls.c
 * Minimal external ls utility scaffold.
 */

#include <unistd.h>
#include "dirent.h"
#include "posix_syscall.h"

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
    struct dirent ents[32];

    if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
        path = argv[1];
    }

    long n = posix_readdir(path, ents, sizeof(ents));
    if (n < 0) {
        put_str("ls: readdir failed\n");
        return 1;
    }

    int count = (int)(n / (long)sizeof(struct dirent));
    for (int i = 0; i < count; i++) {
        if (ents[i].d_name[0] == '\0') {
            continue;
        }
        put_str(ents[i].d_name);
        if (ents[i].d_type == DT_DIR) {
            put_str("/");
        }
        put_str("\n");
    }
    return 0;
}
