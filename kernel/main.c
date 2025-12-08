#include "../include/console.h"
#include "../include/ports.h"
#include "../include/gdt.h"
#include "../include/idt.h"
#include "../include/isr.h"
#include "../include/pic.h"

/* Счётчик тиков таймера */
static uint32_t timer_count = 0;

/* Состояние модификаторов */
static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;
static uint8_t alt_pressed = 0;

/* Таблица преобразования scan-кодов в символы (US QWERTY) */
static const char scancode_to_char_normal[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6',  /* 0x00-0x07 */
    '7', '8', '9', '0', '-', '=', 0,   0,    /* 0x08-0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',  /* 0x10-0x17 */
    'o', 'p', '[', ']', 0,   0,   'a', 's',  /* 0x18-0x1F */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',  /* 0x20-0x27 */
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', /* 0x28-0x2F */
    'b', 'n', 'm', ',', '.', '/', 0,   0,    /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,    /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x40-0x47 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x48-0x4F */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x50-0x57 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x58-0x5F */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x60-0x67 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x68-0x6F */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x70-0x77 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x78-0x7F */
};

static const char scancode_to_char_shift[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^',  /* 0x00-0x07 */
    '&', '*', '(', ')', '_', '+', 0,   0,    /* 0x08-0x0F */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',  /* 0x10-0x17 */
    'O', 'P', '{', '}', 0,   0,   'A', 'S',  /* 0x18-0x1F */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',  /* 0x20-0x27 */
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V', /* 0x28-0x2F */
    'B', 'N', 'M', '<', '>', '?', 0,   0,    /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,    /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x40-0x47 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x50-0x57 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x60-0x67 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x68-0x6F */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x70-0x77 */
    0,   0,   0,   0,   0,   0,   0,   0,    /* 0x78-0x7F */
};

/* IRQ-обработчик таймера (IRQ0) */
static void timer_handler(struct registers* regs)
{
    (void)regs;
    timer_count++;
    /* Убрал вывод таймера - он мешает */
}

/* IRQ-обработчик клавиатуры */
static void keyboard_handler(struct registers* regs)
{
    (void)regs;
    uint8_t sc = inb(0x60);
    uint8_t is_break = (sc & 0x80) != 0;
    uint8_t sc_code = sc & 0x7F;
    
    /* Обработка модификаторов */
    if (sc_code == 0x2A || sc_code == 0x36)  /* Left/Right Shift */
    {
        shift_pressed = !is_break;
        return;
    }
    if (sc_code == 0x1D)  /* Ctrl */
    {
        ctrl_pressed = !is_break;
        return;
    }
    if (sc_code == 0x38)  /* Alt */
    {
        alt_pressed = !is_break;
        return;
    }
    
    /* Обрабатываем только нажатия (make), не отпускания (break) */
    if (is_break)
        return;
    
    /* Преобразование scan-кода в символ */
    char ch = 0;
    if (shift_pressed && sc_code < 128)
        ch = scancode_to_char_shift[sc_code];
    else if (sc_code < 128)
        ch = scancode_to_char_normal[sc_code];
    
    /* Специальные клавиши */
    if (sc_code == 0x1C)  /* Enter */
    {
        kputc('\n');
        return;
    }
    if (sc_code == 0x0E)  /* Backspace */
    {
        kputc('\b');
        kputc(' ');
        kputc('\b');
        return;
    }
    
    /* Вывод символа */
    if (ch != 0)
        kputc(ch);
}

/* Инициализация PS/2 контроллера клавиатуры */
static void kbd_init(void)
{
    kputs("[KB] Step 1: Flushing buffer...\n");
    /* Очистить выходной буфер */
    while (inb(0x64) & 0x01)
        (void)inb(0x60);
    
    kputs("[KB] Step 2: Waiting for controller ready...\n");
    /* Ждать, пока контроллер готов к записи */
    int timeout = 100000;
    while ((inb(0x64) & 0x02) && --timeout) { }
    
    kputs("[KB] Step 3: Enabling keyboard port (0xAE)...\n");
    outb(0x64, 0xAE);  /* Enable keyboard port */
    
    kputs("[KB] Step 4: Waiting for controller ready...\n");
    timeout = 100000;
    while ((inb(0x64) & 0x02) && --timeout) { }
    
    kputs("[KB] Step 5: Sending enable scan command (0xF4)...\n");
    outb(0x60, 0xF4);  /* Enable scanning */
    
    kputs("[KB] Step 6: Waiting for ACK...\n");
    timeout = 100000;
    while (!(inb(0x64) & 0x01) && --timeout) { }
    
    uint8_t ack = inb(0x60);
    kputs("[KB] ACK received: ");
    kprint_hex((uint32_t)ack);
    kputc('\n');
    
    kputs("[KB] Step 7: Flushing buffer again...\n");
    while (inb(0x64) & 0x01)
        (void)inb(0x60);
    
    kputs("[KB] Init complete!\n");
}

void kmain(void)
{
    console_init();
    kputs("RodNIX - Minimal Keyboard Test\n");
    kputs("===============================\n\n");
    
    kputs("[INIT] Setting up GDT...\n");
    gdt_init();
    kputs("[INIT] GDT initialized.\n\n");
    
    kputs("[INIT] Setting up IDT...\n");
    isr_init();
    idt_init();
    kputs("[INIT] IDT initialized.\n\n");
    
    kputs("[INIT] Remapping PIC...\n");
    pic_remap(0x20, 0x28);
    kputs("[INIT] PIC remapped (IRQ 0-7 -> 0x20-0x27, IRQ 8-15 -> 0x28-0x2F).\n\n");
    
    kbd_init();
    
    /* Тестовый обработчик для IRQ0 (таймер) - для проверки работы прерываний */
    kputs("[INIT] Registering timer IRQ handler (IRQ0)...\n");
    register_interrupt_handler(32, timer_handler);
    
    /* Регистрация IRQ-обработчика для клавиатуры (IRQ1 -> IDT 33) */
    kputs("[INIT] Registering keyboard IRQ handler (IRQ1)...\n");
    register_interrupt_handler(33, keyboard_handler);
    
    /* Размаскировать IRQ0 (таймер) и IRQ1 (клавиатура) */
    pic_unmask(0);
    pic_unmask(1);
    
    kputs("[INIT] Keyboard IRQ enabled.\n\n");
    
    /* Включить прерывания */
    kputs("[INIT] Enabling interrupts (sti)...\n");
    __asm__ volatile ("sti");
    kputs("[INIT] Interrupts enabled. Keyboard ready!\n");
    kputs("Press keys in QEMU window.\n\n");
    
    kputs("[INIT] System ready. Interrupts enabled.\n");
    kputs("Press keys in QEMU window.\n\n");
    
    /* Основной цикл - hlt пробуждается прерываниями */
    for (;;)
    {
        __asm__ volatile ("hlt");
    }
}


