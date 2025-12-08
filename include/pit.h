#ifndef _RODNIX_PIT_H
#define _RODNIX_PIT_H

#include "types.h"

/* PIT порты */
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_CMD      0x43

/* PIT частота (1193182 Hz) */
#define PIT_FREQUENCY 1193182

/* Инициализация PIT на заданную частоту (Hz) */
void pit_init(uint32_t frequency);

/* Обработчик прерывания PIT (вызывается из IRQ0 handler) */
void pit_handler(void);

/* Получить количество тиков с момента загрузки */
uint32_t pit_get_ticks(void);

/* Получить время в миллисекундах */
uint32_t pit_get_time_ms(void);

/* Задержка в миллисекундах (блокирующая) */
void pit_sleep_ms(uint32_t ms);

#endif
