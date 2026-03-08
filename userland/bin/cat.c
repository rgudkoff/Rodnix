/*
 * cat.c
 * Minimal external cat utility scaffold.
 */

#include <unistd.h>
#include <fcntl.h>

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

static int cat_one(const char* path)
{
    char buf[128];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        put_str("cat: open failed\n");
        return 1;
    }
    for (;;) {
        long n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        (void)write(STDOUT_FILENO, buf, (unsigned long)n);
    }
    (void)close(fd);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2 || !argv[1] || argv[1][0] == '\0') {
        put_str("cat: path required\n");
        return 1;
    }
    return cat_one(argv[1]);
}
