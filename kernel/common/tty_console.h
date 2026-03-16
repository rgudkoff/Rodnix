#ifndef _RODNIX_COMMON_TTY_CONSOLE_H
#define _RODNIX_COMMON_TTY_CONSOLE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

enum {
    TTY_NCCS = 20,
    TTY_VEOF = 0,
    TTY_VERASE = 3,
    TTY_VKILL = 5,
    TTY_VINTR = 8
};

enum {
    TTY_MODE_ECHO = 0x00000008u,
    TTY_MODE_ECHOCTL = 0x00000040u,
    TTY_MODE_ISIG = 0x00000080u,
    TTY_MODE_ICANON = 0x00000100u,
    TTY_MODE_IEXTEN = 0x00000400u
};

void tty_console_init(void);
int tty_console_read(void* buffer, size_t size, bool echo);
int tty_console_write(const void* buffer, size_t size);
uint32_t tty_console_get_lflag(void);
void tty_console_set_lflag(uint32_t lflag);
uint8_t tty_console_get_cc(uint32_t idx);
void tty_console_set_cc(uint32_t idx, uint8_t value);

#endif /* _RODNIX_COMMON_TTY_CONSOLE_H */
