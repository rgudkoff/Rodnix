#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define FD_STDIN 0
#define FD_STDOUT 1
#define FD_STDERR 2

static long write_buf(int fd, const char* s, uint64_t len)
{
    return posix_write(fd, s, len);
}

static long write_str(int fd, const char* s)
{
    uint64_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return write_buf(fd, s, len);
}

static void write_u64(int fd, uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf(fd, "0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(fd, &buf[i], 1);
    }
}

static void print_cc_value(int fd, uint8_t v)
{
    if (v == 0x7F) {
        (void)write_str(fd, "^?");
        return;
    }
    if (v < 32) {
        char c = (char)(v + '@');
        (void)write_buf(fd, "^", 1);
        (void)write_buf(fd, &c, 1);
        return;
    }
    if (v >= 32 && v <= 126) {
        (void)write_buf(fd, (const char*)&v, 1);
        return;
    }
    write_u64(fd, v);
}

static int parse_u8(const char* s, uint8_t* out)
{
    uint64_t v = 0;
    if (!s || !out || s[0] == '\0') {
        return -1;
    }
    for (uint64_t i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        v = v * 10u + (uint64_t)(c - '0');
        if (v > 255u) {
            return -1;
        }
    }
    *out = (uint8_t)v;
    return 0;
}

static int parse_cc_token(const char* s, uint8_t* out)
{
    if (!s || !out || s[0] == '\0') {
        return -1;
    }
    if (s[0] == '^' && s[1] != '\0' && s[2] == '\0') {
        if (s[1] == '?') {
            *out = 0x7F;
            return 0;
        }
        char c = s[1];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        if (c >= '@' && c <= '_') {
            *out = (uint8_t)(c - '@');
            return 0;
        }
        return -1;
    }
    if (s[1] == '\0') {
        *out = (uint8_t)s[0];
        return 0;
    }
    return parse_u8(s, out);
}

static void print_flags(tcflag_t lflag)
{
    (void)write_str(FD_STDOUT, "lflag:");
    (void)write_str(FD_STDOUT, (lflag & ECHO) ? " echo" : " -echo");
    (void)write_str(FD_STDOUT, (lflag & ECHOCTL) ? " echoctl" : " -echoctl");
    (void)write_str(FD_STDOUT, (lflag & ISIG) ? " isig" : " -isig");
    (void)write_str(FD_STDOUT, (lflag & ICANON) ? " icanon" : " -icanon");
    (void)write_str(FD_STDOUT, (lflag & IEXTEN) ? " iexten" : " -iexten");
    (void)write_str(FD_STDOUT, "\n");
}

static void print_cc(const struct termios* t)
{
    (void)write_str(FD_STDOUT, "cc: intr=");
    print_cc_value(FD_STDOUT, t->c_cc[VINTR]);
    (void)write_str(FD_STDOUT, " erase=");
    print_cc_value(FD_STDOUT, t->c_cc[VERASE]);
    (void)write_str(FD_STDOUT, " kill=");
    print_cc_value(FD_STDOUT, t->c_cc[VKILL]);
    (void)write_str(FD_STDOUT, " eof=");
    print_cc_value(FD_STDOUT, t->c_cc[VEOF]);
    (void)write_str(FD_STDOUT, "\n");
}

static void print_usage(void)
{
    (void)write_str(FD_STDERR, "usage: stty [raw|-raw|cooked|sane] [echo|-echo] [isig|-isig] [icanon|-icanon] [iexten|-iexten] [echoctl|-echoctl] [intr X] [erase X] [kill X] [eof X]\n");
}

int main(int argc, char** argv)
{
    struct termios t;
    int echo_forced_off = 0;
    if (!isatty(FD_STDIN)) {
        (void)write_str(FD_STDERR, "stty: stdin is not a tty\n");
        return 1;
    }
    if (tcgetattr(FD_STDIN, &t) != 0) {
        (void)write_str(FD_STDERR, "stty: tcgetattr failed\n");
        return 1;
    }

    if (argc <= 1) {
        print_flags(t.c_lflag);
        print_cc(&t);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "raw") == 0) {
            /* Safe raw-lite profile for current shell:
             * keep ICANON enabled to avoid breaking line-based input loop.
             */
            t.c_lflag &= ~(ECHOCTL | IEXTEN);
            t.c_lflag |= (ISIG | ICANON);
            continue;
        }
        if (strcmp(a, "-raw") == 0 || strcmp(a, "cooked") == 0) {
            t.c_lflag |= (ECHO | ECHOCTL | ISIG | ICANON | IEXTEN);
            continue;
        }
        if (strcmp(a, "sane") == 0) {
            t.c_lflag |= (ECHO | ECHOCTL | ISIG | ICANON | IEXTEN);
            t.c_cc[VINTR] = 0x03;
            t.c_cc[VERASE] = 0x7F;
            t.c_cc[VKILL] = 0x15;
            t.c_cc[VEOF] = 0x04;
            continue;
        }
        if (strcmp(a, "echo") == 0) {
            t.c_lflag |= ECHO;
            continue;
        }
        if (strcmp(a, "-echo") == 0) {
            t.c_lflag &= ~ECHO;
            echo_forced_off = 1;
            continue;
        }
        if (strcmp(a, "echoctl") == 0) {
            t.c_lflag |= ECHOCTL;
            continue;
        }
        if (strcmp(a, "-echoctl") == 0) {
            t.c_lflag &= ~ECHOCTL;
            continue;
        }
        if (strcmp(a, "isig") == 0) {
            t.c_lflag |= ISIG;
            continue;
        }
        if (strcmp(a, "-isig") == 0) {
            t.c_lflag &= ~ISIG;
            continue;
        }
        if (strcmp(a, "icanon") == 0) {
            t.c_lflag |= ICANON;
            continue;
        }
        if (strcmp(a, "-icanon") == 0) {
            t.c_lflag &= ~ICANON;
            continue;
        }
        if (strcmp(a, "iexten") == 0) {
            t.c_lflag |= IEXTEN;
            continue;
        }
        if (strcmp(a, "-iexten") == 0) {
            t.c_lflag &= ~IEXTEN;
            continue;
        }
        if (strcmp(a, "intr") == 0 || strcmp(a, "erase") == 0 ||
            strcmp(a, "kill") == 0 || strcmp(a, "eof") == 0) {
            if (i + 1 >= argc) {
                print_usage();
                return 1;
            }
            uint8_t v = 0;
            if (parse_cc_token(argv[++i], &v) != 0) {
                (void)write_str(FD_STDERR, "stty: invalid control character value\n");
                return 1;
            }
            if (strcmp(a, "intr") == 0) {
                t.c_cc[VINTR] = v;
            } else if (strcmp(a, "erase") == 0) {
                t.c_cc[VERASE] = v;
            } else if (strcmp(a, "kill") == 0) {
                t.c_cc[VKILL] = v;
            } else {
                t.c_cc[VEOF] = v;
            }
            continue;
        }

        print_usage();
        return 1;
    }

    if (tcsetattr(FD_STDIN, TCSANOW, &t) != 0) {
        (void)write_str(FD_STDERR, "stty: tcsetattr failed\n");
        return 1;
    }
    if (echo_forced_off) {
        (void)write_str(FD_STDERR, "stty: echo is OFF; input is now hidden. Type 'stty sane' and press Enter to restore.\n");
    }
    return 0;
}
