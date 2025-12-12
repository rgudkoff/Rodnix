/**
 * @file keyboard.h
 * @brief PS/2 Keyboard interface
 */

#ifndef _RODNIX_ARCH_X86_64_KEYBOARD_H
#define _RODNIX_ARCH_X86_64_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize keyboard */
int keyboard_init(void);

/* Read functions */
char keyboard_read_char(void);
int keyboard_read_line(char* buffer, uint32_t size);
bool keyboard_has_input(void);
void keyboard_flush(void);

#endif /* _RODNIX_ARCH_X86_64_KEYBOARD_H */

