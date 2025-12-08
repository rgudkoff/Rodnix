#ifndef _RODNIX_KEYBOARD_H
#define _RODNIX_KEYBOARD_H

void keyboard_init(void);
void keyboard_set_handler(void (*handler)(char));

#endif

