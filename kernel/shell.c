#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "pit.h"
#include "debug.h"

#define SHELL_BUFSZ 128

static char line[SHELL_BUFSZ];
static int  llen = 0;

static void prompt(void)
{
    kputs("rodnix> ");
}

static void clear_line(void)
{
    for (int i = 0; i < SHELL_BUFSZ; ++i) line[i] = 0;
    llen = 0;
}

static void handle_cmd(void)
{
    line[llen] = 0;
    if (llen == 0) { prompt(); return; }

    if (line[0] == 'h' && line[1] == 'e' && line[2] == 'l' && line[3] == 'p' && line[4] == 0)
    {
        kputs("cmds: help, ticks\n");
    }
    else if (line[0] == 't' && line[1] == 'i' && line[2] == 'c' && line[3] == 'k' && line[4] == 's' && line[5] == 0)
    {
        kputs("ticks=");
        kprint_dec((uint32_t)pit_ticks());
        kputc('\n');
    }
    else
    {
        kputs("unknown cmd\n");
    }
    prompt();
}

static void on_key(char c)
{
    if (c == '\r') c = '\n';

    if (c == '\b')
    {
        if (llen > 0)
        {
            llen--;
            kputs("\b \b");
        }
        return;
    }

    if (c == '\n')
    {
        kputc('\n');
        handle_cmd();
        clear_line();
        return;
    }

    if (llen < SHELL_BUFSZ - 1)
    {
        line[llen++] = c;
        kputc(c);
    }
}

void shell_init(void)
{
    clear_line();
    keyboard_set_handler(on_key);
    prompt();
}

