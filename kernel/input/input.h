/**
 * @file input.h
 * @brief InputCore - system input layer
 * 
 * InputCore is the system input layer, which:
 * - receives raw events from drivers
 * - maintains keyboard state
 * - translates scancode → character
 * - provides stable API to kernel
 * 
 * Architecture:
 * [ hardware driver ] → [ InputCore ] → [ shell / console / UI ]
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Initialize keyboard subsystem */
void input_init_keyboard(void);

/* Entry point for drivers: raw events */
void input_push_scancode(uint16_t scancode, bool pressed);

/* API for consumers (shell, console) */
bool input_has_char(void);
int  input_read_char(void);                  /* -1 if no character */
size_t input_read_line(char *buf, size_t n); /* blocking line read */

/* Process queued scan codes from interrupt handler */
void input_process_queue(void);


