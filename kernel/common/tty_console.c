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

static void tty_flush_line_to_cooked(void)
{
    for (size_t i = 0; i < tty_line_len; i++) {
        if (!tty_enqueue(tty_line[i])) {
            break;
        }
    }
    tty_enqueue('\n');
    tty_line_len = 0;
}

static void tty_process_input_char(int c, bool echo)
{
    if (c == '\r') {
        c = '\n';
    }

    if (c == '\b' || c == 0x7F) {
        if (tty_line_len > 0) {
            tty_line_len--;
            if (echo) {
                tty_echo_backspace();
            }
        }
        return;
    }

    if (c == 0x15) { /* Ctrl-U */
        while (tty_line_len > 0) {
            tty_line_len--;
            if (echo) {
                tty_echo_backspace();
            }
        }
        return;
    }

    if (c == 0x03) { /* Ctrl-C */
        tty_line_len = 0;
        tty_enqueue('\n');
        if (echo) {
            kputc('^');
            kputc('C');
            kputc('\n');
        }
        return;
    }

    if (c == 0x04) { /* Ctrl-D */
        if (tty_line_len == 0) {
            tty_eof_pending = true;
        }
        return;
    }

    if (c == '\n') {
        if (echo) {
            kputc('\n');
        }
        tty_flush_line_to_cooked();
        return;
    }

    if (c == '\t' || (c >= 32 && c <= 126)) {
        if (tty_line_len + 1 < TTY_LINE_MAX) {
            tty_line[tty_line_len++] = (char)c;
            if (echo) {
                kputc((char)c);
            }
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
