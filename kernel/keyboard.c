#include "keyboard.h"
#include "ports.h"
#include "console.h"
#include "isr.h"
#include "irq.h"

static const char keymap[128] = {
    0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v','b','n',
    'm',',','.','/', 0, '*', 0,' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void (*key_handler)(char) = 0;

static inline void kbd_wait_write(void)
{
    while (inb(0x64) & 0x02) { }
}

static inline void kbd_wait_read(void)
{
    while (!(inb(0x64) & 0x01)) { }
}

static inline void kbd_flush_obf(void)
{
    while (inb(0x64) & 0x01)
        (void)inb(0x60);
}

static void kbd_enable_irq_line(void)
{
    /* Разрешить IRQ1 в контроллере 8042 (command byte bit0) */
    kbd_wait_write();
    outb(0x64, 0x20);   /* read command byte */
    kbd_wait_read();
    uint8_t cmd = inb(0x60);
    cmd |= 0x01;        /* enable IRQ1 */

    kbd_wait_write();
    outb(0x64, 0x60);   /* write command byte */
    kbd_wait_write();
    outb(0x60, cmd);
}

static void kbd_enable_irq(void)
{
    /* Разрешить первый PS/2 порт и включить скан-коды */
    kbd_flush_obf();
    kbd_enable_irq_line();

    kbd_wait_write();
    outb(0x64, 0xAE);     /* enable keyboard port */

    kbd_wait_write();
    outb(0x60, 0xF4);     /* enable scanning */
    kbd_wait_read();
    (void)inb(0x60);      /* ACK */
}

static void keyboard_handler(registers_t* r)
{
    (void)r;
    uint8_t sc = inb(0x60);
    if (!(sc & 0x80))
    {
        char c = keymap[sc];
        if (c)
        {
            if (key_handler)
                key_handler(c);
            else
                kputc(c);
        }
    }
}

void keyboard_init(void)
{
    register_interrupt_handler(33, keyboard_handler);
    irq_enable(1);
    kbd_enable_irq();
}

void keyboard_set_handler(void (*handler)(char))
{
    key_handler = handler;
}

