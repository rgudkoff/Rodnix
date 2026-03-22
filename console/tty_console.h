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
    TTY_VINTR = 8,
    TTY_VTIME = 16,
    TTY_VMIN = 17
};

enum {
    TTY_MODE_ECHO = 0x00000008u,
    TTY_MODE_ECHOCTL = 0x00000040u,
    TTY_MODE_ISIG = 0x00000080u,
    TTY_MODE_ICANON = 0x00000100u,
    TTY_MODE_IEXTEN = 0x00000400u
};

enum {
    TTY_IFLAG_INLCR = 0x00000001u,
    TTY_IFLAG_IGNCR = 0x00000002u,
    TTY_IFLAG_ICRNL = 0x00000004u,
};

enum {
    TTY_OFLAG_OPOST = 0x00000001u,
    TTY_OFLAG_ONLCR = 0x00000002u,
};

void tty_console_init(void);
int tty_console_read(void* buffer, size_t size, bool echo);
int tty_console_write(const void* buffer, size_t size);
bool tty_console_poll_readable(void);
uint32_t tty_console_get_iflag(void);
void tty_console_set_iflag(uint32_t iflag);
uint32_t tty_console_get_oflag(void);
void tty_console_set_oflag(uint32_t oflag);
uint32_t tty_console_get_lflag(void);
void tty_console_set_lflag(uint32_t lflag);
uint8_t tty_console_get_cc(uint32_t idx);
void tty_console_set_cc(uint32_t idx, uint8_t value);
void tty_console_get_winsize(uint16_t* rows, uint16_t* cols,
                             uint16_t* xpixel, uint16_t* ypixel);
void tty_console_set_winsize(uint16_t rows, uint16_t cols,
                             uint16_t xpixel, uint16_t ypixel);
uint64_t tty_console_get_fg_pgrp(void);
void tty_console_set_fg_pgrp(uint64_t pgrp);

#endif /* _RODNIX_COMMON_TTY_CONSOLE_H */
