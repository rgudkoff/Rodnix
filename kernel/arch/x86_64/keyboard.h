/**
 * @file keyboard.h
 * @brief PS/2 Keyboard interface
 */

#ifndef _RODNIX_ARCH_X86_64_KEYBOARD_H
#define _RODNIX_ARCH_X86_64_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

/* Hardware initialization (called by Fabric driver) */
void keyboard_hw_init(void);

/* Buffer management (for Fabric driver) */
void keyboard_buffer_put_raw(uint8_t scan_code);

/* Read functions */
char keyboard_read_char(void);
int keyboard_read_line(char* buffer, uint32_t size);
bool keyboard_has_input(void);
void keyboard_flush(void);

#endif /* _RODNIX_ARCH_X86_64_KEYBOARD_H */

