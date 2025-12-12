/**
 * @file input.h
 * @brief InputCore - системный слой ввода (аналог IOHIDSystem в XNU)
 * 
 * InputCore - это системный слой ввода, который:
 * - принимает сырые события от драйверов
 * - ведёт состояние клавиатуры
 * - переводит scancode → символ
 * - предоставляет стабильный API ядру
 * 
 * Архитектура:
 * [ hardware driver ] → [ InputCore ] → [ shell / console / UI ]
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Инициализация подсистемы клавиатуры */
void input_init_keyboard(void);

/* Вход для драйверов: сырые события */
void input_push_scancode(uint16_t scancode, bool pressed);

/* API для потребителей (shell, console) */
bool input_has_char(void);
int  input_read_char(void);                  /* -1 если нет символа */
size_t input_read_line(char *buf, size_t n); /* блокирующее чтение строки */

