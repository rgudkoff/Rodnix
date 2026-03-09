#include <stdint.h>
#include <sys/fcntl.h>
#include <unistd.h>

#define FD_STDOUT 1

static long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

static long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

static int str_eq(const char* a, const char* b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int main(void)
{
    char cwd[128];
    int p[2] = {-1, -1};
    char c = 0;

    if (!getcwd(cwd, sizeof(cwd))) {
        (void)write_str("fsapitest: getcwd initial failed\n");
        return 1;
    }

    if (mkdir("/tmp", 0755) != 0) {
        (void)write_str("fsapitest: mkdir /tmp failed\n");
        return 1;
    }
    if (mkdir("/tmp/fsapi", 0755) != 0) {
        (void)write_str("fsapitest: mkdir /tmp/fsapi failed\n");
        return 1;
    }

    if (chdir("/tmp/fsapi") != 0) {
        (void)write_str("fsapitest: chdir failed\n");
        return 1;
    }
    if (!getcwd(cwd, sizeof(cwd)) || !str_eq(cwd, "/tmp/fsapi")) {
        (void)write_str("fsapitest: getcwd after chdir mismatch\n");
        return 1;
    }

    {
        int fd = open("f.txt", O_CREAT | O_WRONLY | O_TRUNC);
        if (fd < 0) {
            (void)write_str("fsapitest: open failed\n");
            return 1;
        }
        int fd2 = dup(fd);
        if (fd2 < 0) {
            (void)write_str("fsapitest: dup failed\n");
            (void)close(fd);
            return 1;
        }
        if (write(fd2, "ok", 2) != 2) {
            (void)write_str("fsapitest: dup write failed\n");
            (void)close(fd2);
            (void)close(fd);
            return 1;
        }
        if (dup2(fd2, fd) < 0) {
            (void)write_str("fsapitest: dup2 failed\n");
            (void)close(fd2);
            (void)close(fd);
            return 1;
        }
        if (close(fd2) != 0 || close(fd) != 0) {
            (void)write_str("fsapitest: close failed\n");
            return 1;
        }
    }

    {
        int fd = open("f.txt", O_WRONLY);
        if (fd < 0) {
            (void)write_str("fsapitest: open for ftruncate failed\n");
            return 1;
        }
        if (ftruncate(fd, 1) != 0) {
            (void)write_str("fsapitest: ftruncate failed\n");
            (void)close(fd);
            return 1;
        }
        if (close(fd) != 0) {
            (void)write_str("fsapitest: close after ftruncate failed\n");
            return 1;
        }
        if (truncate("f.txt", 3) != 0) {
            (void)write_str("fsapitest: truncate failed\n");
            return 1;
        }
    }

    {
        int fd = open("f.txt", O_RDONLY);
        char chk[4];
        if (fd < 0) {
            (void)write_str("fsapitest: open verify size failed\n");
            return 1;
        }
        if (read(fd, chk, 4) != 3) {
            (void)write_str("fsapitest: truncate size mismatch\n");
            (void)close(fd);
            return 1;
        }
        if (close(fd) != 0) {
            (void)write_str("fsapitest: close verify size failed\n");
            return 1;
        }
    }

    if (pipe(p) != 0) {
        (void)write_str("fsapitest: pipe failed\n");
        return 1;
    }
    {
        int w2 = dup2(p[1], 9);
        if (w2 != 9) {
            (void)write_str("fsapitest: dup2(pipe) failed\n");
            (void)close(p[0]);
            (void)close(p[1]);
            return 1;
        }
        if (write(9, "Z", 1) != 1) {
            (void)write_str("fsapitest: write dup2(pipe) failed\n");
            (void)close(p[0]);
            (void)close(p[1]);
            (void)close(9);
            return 1;
        }
        (void)close(9);
        (void)close(p[1]);
        if (read(p[0], &c, 1) != 1 || c != 'Z') {
            (void)write_str("fsapitest: read pipe failed\n");
            (void)close(p[0]);
            return 1;
        }
        (void)close(p[0]);
    }

    if (rename("f.txt", "g.txt") != 0) {
        (void)write_str("fsapitest: rename failed\n");
        return 1;
    }

    if (unlink("g.txt") != 0) {
        (void)write_str("fsapitest: unlink failed\n");
        return 1;
    }

    if (chdir("/") != 0) {
        (void)write_str("fsapitest: chdir / failed\n");
        return 1;
    }
    if (rmdir("/tmp/fsapi") != 0) {
        (void)write_str("fsapitest: rmdir failed\n");
        return 1;
    }

    (void)write_str("fsapitest: PASS\n");
    return 0;
}
