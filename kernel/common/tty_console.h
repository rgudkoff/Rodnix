#ifndef _RODNIX_COMMON_TTY_CONSOLE_H
#define _RODNIX_COMMON_TTY_CONSOLE_H

#include <stddef.h>
#include <stdbool.h>

void tty_console_init(void);
int tty_console_read(void* buffer, size_t size, bool echo);
int tty_console_write(const void* buffer, size_t size);

#endif /* _RODNIX_COMMON_TTY_CONSOLE_H */
