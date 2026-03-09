#include "tty_console.h"
#include "../input/input.h"
#include "scheduler.h"
#include "../../include/console.h"
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
static uint32_t tty_lflag = 0;
static uint8_t tty_cc[TTY_NCCS];

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

    if (c == '\t' || (c >= 32 && c <= 126)) {
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
    tty_lflag = TTY_MODE_ECHO |
                TTY_MODE_ECHOCTL |
                TTY_MODE_ISIG |
                TTY_MODE_ICANON |
                TTY_MODE_IEXTEN;
    for (uint32_t i = 0; i < TTY_NCCS; i++) {
        tty_cc[i] = 0;
    }
    tty_cc[TTY_VEOF] = 0x04;
    tty_cc[TTY_VERASE] = 0x7F;
    tty_cc[TTY_VKILL] = 0x15;
    tty_cc[TTY_VINTR] = 0x03;
}

int tty_console_read(void* buffer, size_t size, bool echo)
{
    char* out = (char*)buffer;
    size_t nread = 0;

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
            if (nread > 0) {
                break;
            }
            scheduler_ast_check();
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
        kputc(s[i]);
    }
    return (int)size;
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
