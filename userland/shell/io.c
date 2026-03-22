#include "shell_internal.h"

static char shell_history[SH_HISTORY_MAX][SH_LINE_MAX];
static int shell_history_count = 0;

static void shell_history_store(const char* line)
{
    int len = 0;

    if (!line || line[0] == '\0') {
        return;
    }

    if (shell_history_count > 0 &&
        str_eq(shell_history[shell_history_count - 1], line)) {
        return;
    }

    while (line[len] != '\0' && len + 1 < SH_LINE_MAX) {
        len++;
    }

    if (shell_history_count < SH_HISTORY_MAX) {
        for (int i = 0; i < len; i++) {
            shell_history[shell_history_count][i] = line[i];
        }
        shell_history[shell_history_count][len] = '\0';
        shell_history_count++;
        return;
    }

    for (int i = 1; i < SH_HISTORY_MAX; i++) {
        int j = 0;
        while (shell_history[i][j] != '\0' && j + 1 < SH_LINE_MAX) {
            shell_history[i - 1][j] = shell_history[i][j];
            j++;
        }
        shell_history[i - 1][j] = '\0';
    }
    for (int i = 0; i < len; i++) {
        shell_history[SH_HISTORY_MAX - 1][i] = line[i];
    }
    shell_history[SH_HISTORY_MAX - 1][len] = '\0';
}

static void shell_copy_line(char* dst, int dst_sz, const char* src)
{
    int i = 0;

    if (!dst || dst_sz <= 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1 < dst_sz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int shell_line_len(const char* s)
{
    int len = 0;
    if (!s) {
        return 0;
    }
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void shell_redraw_input(const char* line)
{
    (void)write_str("\r\x1b[2K");
    shell_print_prompt();
    if (line && line[0] != '\0') {
        (void)write_str(line);
    }
}

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
    int history_index = shell_history_count;
    unsigned char ch = 0;
    char draft[SH_LINE_MAX];

    draft[0] = '\0';

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
            shell_history_store(out);
            return pos;
        }

        if (ch == 0x7f || ch == 0x08) {
            if (pos > 0) {
                pos--;
                out[pos] = '\0';
            }
            continue;
        }

        if (ch == 0x1Bu) {
            unsigned char seq1 = 0;
            unsigned char seq2 = 0;
            if (shell_read_stdin_byte(&seq1) <= 0 || shell_read_stdin_byte(&seq2) <= 0) {
                continue;
            }
            if (seq1 != '[') {
                continue;
            }
            if (seq2 == 'A') {
                if (shell_history_count <= 0 || history_index <= 0) {
                    continue;
                }
                if (history_index == shell_history_count) {
                    shell_copy_line(draft, (int)sizeof(draft), out);
                }
                history_index--;
                shell_copy_line(out, out_len, shell_history[history_index]);
                pos = shell_line_len(out);
                shell_redraw_input(out);
                continue;
            }
            if (seq2 == 'B') {
                if (history_index >= shell_history_count) {
                    continue;
                }
                history_index++;
                if (history_index == shell_history_count) {
                    shell_copy_line(out, out_len, draft);
                } else {
                    shell_copy_line(out, out_len, shell_history[history_index]);
                }
                pos = shell_line_len(out);
                shell_redraw_input(out);
                continue;
            }
            continue;
        }

        if (ch < 0x20u || ch > 0x7Eu) {
            continue;
        }

        if (pos + 1 < out_len) {
            if (history_index != shell_history_count) {
                history_index = shell_history_count;
                draft[0] = '\0';
            }
            out[pos++] = (char)ch;
            out[pos] = '\0';
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
    if (shell_cwd[0] != '\0') {
        (void)write_str(shell_cwd);
    } else {
        (void)write_str("/");
    }
    (void)write_str(" ");
    if (uid == 0) {
        (void)write_str("# ");
    } else {
        (void)write_str("$ ");
    }
}
