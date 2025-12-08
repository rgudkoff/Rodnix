#include "keyboard.h"
#include "ports.h"
#include "console.h"
#include "isr.h"

static const char keymap[128] = {
    0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v','b','n',
    'm',',','.','/', 0, '*', 0,' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void keyboard_handler(registers_t* r)
{
    (void)r;
    uint8_t sc = inb(0x60);
    if (!(sc & 0x80))
    {
        char c = keymap[sc];
        if (c) kputc(c);
    }
}

void keyboard_init(void)
{
    register_interrupt_handler(33, keyboard_handler);
}

