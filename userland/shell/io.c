#include "shell_internal.h"

long write_buf(const char* s, uint64_t len)
{
    return posix_write(FD_STDOUT, s, len);
}

long write_str(const char* s)
{
    uint64_t len = 0;
    while (s[len]) {
        len++;
    }
    return write_buf(s, len);
}

void write_u64(uint64_t v)
{
    char buf[32];
    int i = 0;
    if (v == 0) {
        (void)write_buf("0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        i--;
        (void)write_buf(&buf[i], 1);
    }
}

void write_hex_u32(uint32_t v)
{
    static const char table[] = "0123456789ABCDEF";
    char out[8];
    for (int i = 7; i >= 0; i--) {
        out[i] = table[v & 0x0F];
        v >>= 4;
    }
    (void)write_buf(out, 8);
}

void write_mem_short(uint64_t bytes)
{
    const uint64_t KB = 1024ULL;
    const uint64_t MB = 1024ULL * 1024ULL;
    if (bytes >= MB) {
        write_u64(bytes / MB);
        (void)write_str(" MB");
        return;
    }
    if (bytes >= KB) {
        write_u64(bytes / KB);
        (void)write_str(" KB");
        return;
    }
    write_u64(bytes);
    (void)write_str(" B");
}

int str_eq(const char* a, const char* b)
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

int str_starts(const char* s, const char* p)
{
    if (!s || !p) {
        return 0;
    }
    while (*p) {
        if (*s != *p) {
            return 0;
        }
        s++;
        p++;
    }
    return 1;
}

void sanitize_cmd_token(char* s)
{
    if (!s) {
        return;
    }
    int w = 0;
    for (int r = 0; s[r] != '\0'; r++) {
        unsigned char c = (unsigned char)s[r];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.' || c == '/') {
            s[w++] = (char)c;
        }
    }
    s[w] = '\0';
}

static long shell_read_stdin_byte(unsigned char* ch)
{
    long n;
    /* Temporary workaround:
     * interactive stdin read uses int 0x80 because fast syscall path for
     * blocking TTY read is currently unstable (hang / no proper wakeup).
     * Do not switch to fast path until kernel-side tty read contract is fixed.
     */
    __asm__ volatile (
        "int $0x80"
        : "=a"(n)
        : "a"(POSIX_SYS_READ), "D"(FD_STDIN), "S"((long)(uintptr_t)ch), "d"(1L)
        : "memory"
    );
    return n;
}

int read_line(char* out, int out_len)
{
    int pos = 0;
    unsigned char ch = 0;

    if (!out || out_len <= 1) {
        return -1;
    }

    for (;;) {
        long n = shell_read_stdin_byte(&ch);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }

        if (ch == '\r' || ch == '\n') {
            out[pos] = '\0';
            return pos;
        }

        if (ch == 0x7f || ch == 0x08) {
            if (pos > 0) {
                pos--;
            }
            continue;
        }

        if (ch < 0x20u || ch > 0x7Eu) {
            continue;
        }

        if (pos + 1 < out_len) {
            out[pos++] = (char)ch;
        }
    }
}

void shell_print_prompt(void)
{
    if (shell_ps1[0] != '\0') {
        (void)write_str(shell_ps1);
        return;
    }

    long uid = rdnx_syscall0(POSIX_SYS_GETEUID);
    (void)write_str(" ");
    if (uid == 0) {
        (void)write_str("# ");
    } else {
        (void)write_str("$ ");
    }
}
