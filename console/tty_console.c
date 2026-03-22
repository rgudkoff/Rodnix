#include "tty_console.h"
#include "../kernel/input/input.h"
#include "../sched/scheduler.h"
#include "../include/console.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define TTY_LINE_MAX   256
#define TTY_COOKED_MAX 512

static char tty_line[TTY_LINE_MAX];
static size_t tty_line_len = 0;

static char tty_cooked[TTY_COOKED_MAX];
static size_t tty_cooked_head = 0;
static size_t tty_cooked_tail = 0;
static size_t tty_cooked_count = 0;

static bool tty_eof_pending = false;
static uint32_t tty_iflag = 0;
static uint32_t tty_oflag = 0;
static uint32_t tty_lflag = 0;
static uint8_t tty_cc[TTY_NCCS];
static uint64_t tty_fg_pgrp = 0;
static uint16_t tty_ws_row = 25;
static uint16_t tty_ws_col = 80;
static uint16_t tty_ws_xpixel = 0;
static uint16_t tty_ws_ypixel = 0;
static bool tty_trace_esc = false;
static char tty_esc_buf[64];
static size_t tty_esc_len = 0;

static size_t tty_append_u32(char* buf, size_t pos, size_t cap, uint32_t value)
{
    char tmp[16];
    size_t len = 0;

    if (value == 0) {
        if (pos < cap) {
            buf[pos++] = '0';
        }
        return pos;
    }

    while (value > 0 && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (len > 0) {
        if (pos < cap) {
            buf[pos++] = tmp[--len];
        } else {
            break;
        }
    }
    return pos;
}

static void tty_send_cpr_response(void)
{
    char resp[32];
    uint32_t row = 0;
    uint32_t col = 0;
    console_get_cursor_position(&row, &col);
    size_t len = 0;

    resp[len++] = '\x1b';
    resp[len++] = '[';
    len = tty_append_u32(resp, len, sizeof(resp), row + 1u);
    if (len < sizeof(resp)) {
        resp[len++] = ';';
    }
    len = tty_append_u32(resp, len, sizeof(resp), col + 1u);
    if (len < sizeof(resp)) {
        resp[len++] = 'R';
    }
    (void)input_inject_bytes(resp, len);
}

static void tty_trace_flush(void)
{
    if (!tty_trace_esc || tty_esc_len == 0) {
        tty_esc_len = 0;
        return;
    }

    tty_esc_buf[tty_esc_len] = '\0';
    if (tty_esc_len == 5 &&
        tty_esc_buf[0] == '^' &&
        tty_esc_buf[1] == '[' &&
        tty_esc_buf[2] == '[' &&
        tty_esc_buf[3] == '6' &&
        tty_esc_buf[4] == 'n') {
        tty_send_cpr_response();
    }
    tty_esc_len = 0;
    tty_trace_esc = false;
}

static void tty_trace_char(char c)
{
    unsigned char uc = (unsigned char)c;

    if (!tty_trace_esc) {
        if (uc == 0x1Bu) {
            tty_trace_esc = true;
            tty_esc_len = 0;
            tty_esc_buf[tty_esc_len++] = '^';
            tty_esc_buf[tty_esc_len++] = '[';
        }
        return;
    }

    if (tty_esc_len + 4 >= sizeof(tty_esc_buf)) {
        tty_trace_flush();
        return;
    }

    if (uc >= 32u && uc <= 126u) {
        tty_esc_buf[tty_esc_len++] = (char)uc;
    } else {
        tty_esc_buf[tty_esc_len++] = '<';
        const char hex[] = "0123456789ABCDEF";
        tty_esc_buf[tty_esc_len++] = hex[(uc >> 4) & 0x0Fu];
        tty_esc_buf[tty_esc_len++] = hex[uc & 0x0Fu];
        tty_esc_buf[tty_esc_len++] = '>';
    }

    if ((uc >= '@' && uc <= '~') || uc == 0x07u) {
        tty_trace_flush();
    }
}

static bool tty_enqueue(char c)
{
    if (tty_cooked_count >= TTY_COOKED_MAX) {
        return false;
    }
    tty_cooked[tty_cooked_tail] = c;
    tty_cooked_tail = (tty_cooked_tail + 1) % TTY_COOKED_MAX;
    tty_cooked_count++;
    return true;
}

static bool tty_dequeue(char* out)
{
    if (!out || tty_cooked_count == 0) {
        return false;
    }
    *out = tty_cooked[tty_cooked_head];
    tty_cooked_head = (tty_cooked_head + 1) % TTY_COOKED_MAX;
    tty_cooked_count--;
    return true;
}

static void tty_echo_backspace(void)
{
    kputc('\b');
    kputc(' ');
    kputc('\b');
}

static void tty_echo_control(unsigned char c)
{
    if ((c & 0x80u) != 0) {
        return;
    }
    kputc('^');
    if (c == 0x7Fu) {
        kputc('?');
    } else {
        kputc((char)(c ^ 0x40u));
    }
}

static void tty_echo_char(unsigned char c, bool do_echo)
{
    if (!do_echo) {
        return;
    }
    if (c == '\n') {
        kputc('\n');
        return;
    }
    if (c >= 32u && c <= 126u) {
        kputc((char)c);
        return;
    }
    if ((tty_lflag & TTY_MODE_ECHOCTL) != 0) {
        tty_echo_control(c);
    }
}

static void tty_flush_line_to_cooked(bool add_newline)
{
    for (size_t i = 0; i < tty_line_len; i++) {
        if (!tty_enqueue(tty_line[i])) {
            break;
        }
    }
    if (add_newline) {
        (void)tty_enqueue('\n');
    }
    tty_line_len = 0;
}

static void tty_process_input_char(int c, bool echo)
{
    bool do_echo = echo && ((tty_lflag & TTY_MODE_ECHO) != 0);
    bool canonical = (tty_lflag & TTY_MODE_ICANON) != 0;
    bool isig = (tty_lflag & TTY_MODE_ISIG) != 0;

    if (c == '\r') {
        if ((tty_iflag & TTY_IFLAG_IGNCR) != 0) {
            return;
        }
        if ((tty_iflag & TTY_IFLAG_ICRNL) != 0) {
            c = '\n';
        }
    } else if (c == '\n' && (tty_iflag & TTY_IFLAG_INLCR) != 0) {
        c = '\r';
    }

    if (c == '\r' && (tty_iflag & TTY_IFLAG_ICRNL) != 0) {
        c = '\n';
    }

    if (isig && (uint8_t)c == tty_cc[TTY_VINTR]) {
        tty_line_len = 0;
        (void)tty_enqueue('\n');
        if (do_echo) {
            tty_echo_control((uint8_t)c);
            kputc('\n');
        }
        return;
    }

    if (!canonical) {
        tty_echo_char((uint8_t)c, do_echo);
        (void)tty_enqueue((char)c);
        return;
    }

    if ((uint8_t)c == tty_cc[TTY_VERASE] || c == '\b') {
        if (tty_line_len > 0) {
            tty_line_len--;
            if (do_echo) {
                tty_echo_backspace();
            }
        }
        return;
    }

    if ((uint8_t)c == tty_cc[TTY_VKILL]) {
        while (tty_line_len > 0) {
            tty_line_len--;
            if (do_echo) {
                tty_echo_backspace();
            }
        }
        return;
    }

    if ((uint8_t)c == tty_cc[TTY_VEOF]) {
        if (tty_line_len == 0) {
            tty_eof_pending = true;
        } else {
            tty_flush_line_to_cooked(false);
        }
        return;
    }

    if (c == '\n') {
        tty_echo_char((uint8_t)c, do_echo);
        tty_flush_line_to_cooked(true);
        return;
    }

    if ((uint8_t)c == 0x7Fu ||
        c == '\t' ||
        (c > 0 && c < 32) ||
        (c >= 32 && c <= 126)) {
        if (tty_line_len + 1 < TTY_LINE_MAX) {
            tty_line[tty_line_len++] = (char)c;
            tty_echo_char((uint8_t)c, do_echo);
        }
    }
}

void tty_console_init(void)
{
    tty_line_len = 0;
    tty_cooked_head = 0;
    tty_cooked_tail = 0;
    tty_cooked_count = 0;
    tty_eof_pending = false;
    tty_iflag = TTY_IFLAG_ICRNL;
    tty_oflag = TTY_OFLAG_OPOST | TTY_OFLAG_ONLCR;
    tty_lflag = TTY_MODE_ECHO |
                TTY_MODE_ECHOCTL |
                TTY_MODE_ISIG |
                TTY_MODE_ICANON |
                TTY_MODE_IEXTEN;
    tty_fg_pgrp = 0;
    for (uint32_t i = 0; i < TTY_NCCS; i++) {
        tty_cc[i] = 0;
    }
    tty_cc[TTY_VEOF] = 0x04;
    tty_cc[TTY_VERASE] = 0x7F;
    tty_cc[TTY_VKILL] = 0x15;
    tty_cc[TTY_VINTR] = 0x03;
    tty_cc[TTY_VTIME] = 0;
    tty_cc[TTY_VMIN] = 1;
    tty_ws_row = 25;
    tty_ws_col = 80;
    tty_ws_xpixel = 0;
    tty_ws_ypixel = 0;
}

int tty_console_read(void* buffer, size_t size, bool echo)
{
    char* out = (char*)buffer;
    size_t nread = 0;
    bool canonical = (tty_lflag & TTY_MODE_ICANON) != 0;
    uint8_t vmin = tty_cc[TTY_VMIN];
    uint8_t vtime = tty_cc[TTY_VTIME];
    uint64_t deadline_us = 0;
    bool saw_input = false;

    if (!out) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    while (nread < size) {
        char c = 0;
        if (tty_dequeue(&c)) {
            out[nread++] = c;
            saw_input = true;
            if (!canonical) {
                if (vmin == 0) {
                    break;
                }
                if (nread >= vmin) {
                    break;
                }
                if (vtime > 0) {
                    deadline_us = console_get_uptime_us() + ((uint64_t)vtime * 100000u);
                }
            }
            continue;
        }

        if (tty_eof_pending) {
            tty_eof_pending = false;
            if (nread == 0) {
                return 0;
            }
            break;
        }

        int in = input_read_char();
        if (in < 0) {
            if (!canonical) {
                if (vmin == 0 && vtime == 0) {
                    break;
                }
                if (vtime > 0) {
                    uint64_t now = console_get_uptime_us();
                    if (!saw_input && vmin == 0) {
                        if (deadline_us == 0) {
                            deadline_us = now + ((uint64_t)vtime * 100000u);
                        } else if (now >= deadline_us) {
                            break;
                        }
                    } else if (saw_input && deadline_us != 0 && now >= deadline_us) {
                        break;
                    }
                }
            }
            if (nread > 0) {
                break;
            }
            scheduler_ast_check();
            scheduler_yield();
            __asm__ volatile ("int $32"
                              :
                              :
                              : "rax", "rcx", "rdx", "rsi", "rdi",
                                "r8", "r9", "r10", "r11", "cc", "memory");
            continue;
        }
        tty_process_input_char(in, echo);
    }

    return (int)nread;
}

int tty_console_write(const void* buffer, size_t size)
{
    const char* s = (const char*)buffer;
    if (!s) {
        return -1;
    }
    for (size_t i = 0; i < size; i++) {
        tty_trace_char(s[i]);
        if ((tty_oflag & TTY_OFLAG_OPOST) != 0 &&
            (tty_oflag & TTY_OFLAG_ONLCR) != 0 &&
            s[i] == '\n') {
            kputc('\r');
        }
        kputc(s[i]);
    }
    return (int)size;
}

bool tty_console_poll_readable(void)
{
    if (tty_cooked_count > 0 || tty_eof_pending) {
        return true;
    }

    /*
     * Drive pending input through line discipline so poll() in canonical mode
     * reports readable only when a full line/EOF is available.
     */
    while (input_has_char()) {
        int in = input_read_char();
        if (in < 0) {
            break;
        }
        tty_process_input_char(in, false);
        if (tty_cooked_count > 0 || tty_eof_pending) {
            return true;
        }
    }
    return false;
}

uint32_t tty_console_get_iflag(void)
{
    return tty_iflag;
}

void tty_console_set_iflag(uint32_t iflag)
{
    tty_iflag = iflag;
}

uint32_t tty_console_get_oflag(void)
{
    return tty_oflag;
}

void tty_console_set_oflag(uint32_t oflag)
{
    tty_oflag = oflag;
}

uint32_t tty_console_get_lflag(void)
{
    return tty_lflag;
}

void tty_console_set_lflag(uint32_t lflag)
{
    tty_lflag = lflag;
}

uint8_t tty_console_get_cc(uint32_t idx)
{
    if (idx >= TTY_NCCS) {
        return 0;
    }
    return tty_cc[idx];
}

void tty_console_set_cc(uint32_t idx, uint8_t value)
{
    if (idx >= TTY_NCCS) {
        return;
    }
    tty_cc[idx] = value;
}

void tty_console_get_winsize(uint16_t* rows, uint16_t* cols,
                             uint16_t* xpixel, uint16_t* ypixel)
{
    if (rows) {
        *rows = tty_ws_row;
    }
    if (cols) {
        *cols = tty_ws_col;
    }
    if (xpixel) {
        *xpixel = tty_ws_xpixel;
    }
    if (ypixel) {
        *ypixel = tty_ws_ypixel;
    }
}

void tty_console_set_winsize(uint16_t rows, uint16_t cols,
                             uint16_t xpixel, uint16_t ypixel)
{
    if (rows != 0u) {
        tty_ws_row = rows;
    }
    if (cols != 0u) {
        tty_ws_col = cols;
    }
    tty_ws_xpixel = xpixel;
    tty_ws_ypixel = ypixel;
}

uint64_t tty_console_get_fg_pgrp(void)
{
    return tty_fg_pgrp;
}

void tty_console_set_fg_pgrp(uint64_t pgrp)
{
    tty_fg_pgrp = pgrp;
}
