#include "../include/pit.h"
#include "../include/ports.h"

/* Счётчик тиков PIT */
volatile uint32_t pit_ticks = 0;

/* Частота PIT в Hz */
static uint32_t pit_frequency = 100;

/* Обработчик прерывания PIT (IRQ0) - вызывается из timer_handler */
void pit_handler(void)
{
    /* Инкремент тиков - должен вызываться 100 раз в секунду */
    pit_ticks++;
}

/* Инициализация PIT на заданную частоту (Hz) */
void pit_init(uint32_t frequency)
{
    pit_frequency = frequency;
    pit_ticks = 0;
    
    /* Вычислить делитель для заданной частоты */
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    /* Режим 3 (square wave generator) для канала 0 */
    /* Бит 7-6: канал 0 (00) */
    /* Бит 5-4: режим доступа low/high byte (11) */
    /* Бит 3-1: режим 3 (011) */
    /* Бит 0: binary mode (0) */
    uint8_t cmd = 0x36;  /* 00110110 */
    
    outb(PIT_CMD, cmd);
    
    /* Записать делитель (младший байт, затем старший) */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

/* Получить количество тиков с момента загрузки */
uint32_t pit_get_ticks(void)
{
    return pit_ticks;
}

/* Получить время в миллисекундах */
uint32_t pit_get_time_ms(void)
{
    return (pit_ticks * 1000) / pit_frequency;
}

/* Задержка в миллисекундах (блокирующая) */
void pit_sleep_ms(uint32_t ms)
{
    uint32_t start_ticks = pit_ticks;
    uint32_t target_ticks = start_ticks + (ms * pit_frequency / 1000);
    
    while (pit_ticks < target_ticks)
    {
        __asm__ volatile ("hlt");
    }
}

