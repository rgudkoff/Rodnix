#include "../include/console.h"
#include "../include/ports.h"
#include "../include/gdt.h"
#include "interrupts/compat.h"
#include "interrupts/interrupts.h"
#include "../drivers/timer/pit.h"
#include "../include/memory.h"
#include "../include/ipc.h"
#include "../include/capability.h"
#include "../include/scheduler.h"
#include "../include/pci.h"
#include "../include/paging.h"
#include "../include/pmm.h"
#include "../include/paging.h"
#include "../include/vmm.h"
#include "../include/heap.h"


/* Состояние модификаторов */
static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;
static uint8_t alt_pressed = 0;

/* Shell: буфер ввода */
#define SHELL_BUFFER_SIZE 256
static char shell_buffer[SHELL_BUFFER_SIZE];
static uint32_t shell_buffer_pos = 0;
static uint8_t shell_ready = 0;  /* 1 = команда готова к обработке */

/* Вспомогательные функции для shell */
static int strcmp(const char* s1, const char* s2)
{
    while (*s1 && *s2 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return (int)(*s1) - (int)(*s2);
}

static int strncmp(const char* s1, const char* s2, size_t n)
{
    while (n > 0 && *s1 && *s2 && *s1 == *s2)
    {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return (int)(*s1) - (int)(*s2);
}

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
/* PIT handler теперь регистрируется автоматически в pit_init() */
static void timer_handler(struct registers* regs)
{
    (void)regs;
    /* PIT handler вызывается автоматически через irq_dispatch */
}

/* IRQ-обработчик клавиатуры */
static void keyboard_handler(struct registers* regs)
{
    (void)regs;
    
    /* Debug: проверка, вызывается ли обработчик */
    volatile uint16_t* vga_kb = (volatile uint16_t*)0xB8000;
    static uint8_t kb_counter = 0;
    vga_kb[80*18 + (kb_counter % 80)] = 0x0F4B;  // 'K' (Keyboard interrupt)
    kb_counter++;
    if (kb_counter >= 80) kb_counter = 0;
    
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
        if (shell_buffer_pos > 0)
        {
            shell_buffer[shell_buffer_pos] = '\0';
            shell_ready = 1;
        }
        else
        {
            /* Пустая строка - показать приглашение */
            kputs("rodnix> ");
        }
        return;
    }
    if (sc_code == 0x0E)  /* Backspace */
    {
        if (shell_buffer_pos > 0)
        {
            shell_buffer_pos--;
            kputc('\b');
            kputc(' ');
            kputc('\b');
        }
        return;
    }
    
    /* Добавление символа в буфер */
    if (ch != 0 && shell_buffer_pos < SHELL_BUFFER_SIZE - 1)
    {
        shell_buffer[shell_buffer_pos++] = ch;
        
        /* Debug: показать scan-код */
        vga_kb[80*17 + 0] = 0x0F53;  // 'S' (Scan code)
        vga_kb[80*17 + 1] = 0x0F43;  // 'C' (Code)
        vga_kb[80*17 + 2] = 0x0F30 + ((sc_code >> 4) & 0xF);  // Hex digit 1
        vga_kb[80*17 + 3] = 0x0F30 + (sc_code & 0xF);  // Hex digit 2
        vga_kb[80*17 + 4] = 0x0F43;  // 'C' (Char)
        vga_kb[80*17 + 5] = 0x0F48;  // 'H' (Char)
        vga_kb[80*17 + 6] = (uint16_t)ch | (0x0F << 8);  // Character
        
        /* Используем прямой VGA вывод вместо kputc(), чтобы обойти возможные проблемы */
        static uint8_t vga_x = 0;
        static uint8_t vga_y = 24;
        if (vga_x >= 80)
        {
            vga_x = 0;
            vga_y++;
            if (vga_y >= 25) vga_y = 24;
        }
        vga_kb[vga_y * 80 + vga_x] = (uint16_t)ch | (0x0F << 8);
        vga_x++;
        
        /* Обновить позицию курсора */
        uint16_t pos = vga_y * 80 + vga_x;
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
}

/* Инициализация PS/2 контроллера клавиатуры */
__attribute__((unused)) static void kbd_init(void)
{
    volatile uint16_t* vga_kbd = (volatile uint16_t*)0xB8000;
    vga_kbd[80*0 + 13] = 0x0F4B;  // 'K' (kbd start)
    vga_kbd[80*0 + 14] = 0x0F31;  // '1' (step 1)
    
    /* Очистить выходной буфер с таймаутом */
    int timeout = 1000;  // Ограничиваем количество итераций
    while ((inb(0x64) & 0x01) && --timeout > 0)
        (void)inb(0x60);
    
    vga_kbd[80*0 + 15] = 0x0F32;  // '2' (step 2)
    
    /* Ждать, пока контроллер готов к записи */
    timeout = 100000;
    while ((inb(0x64) & 0x02) && --timeout) { }
    
    vga_kbd[80*0 + 16] = 0x0F33;  // '3' (step 3)
    
    outb(0x64, 0xAE);  /* Enable keyboard port */
    
    vga_kbd[80*0 + 17] = 0x0F34;  // '4' (step 4)
    
    timeout = 100000;
    while ((inb(0x64) & 0x02) && --timeout) { }
    
    vga_kbd[80*0 + 18] = 0x0F35;  // '5' (step 5)
    
    outb(0x60, 0xF4);  /* Enable scanning */
    
    vga_kbd[80*0 + 19] = 0x0F36;  // '6' (step 6)
    
    timeout = 100000;
    while (!(inb(0x64) & 0x01) && --timeout) { }
    
    vga_kbd[80*0 + 20] = 0x0F37;  // '7' (step 7)
    
    uint8_t ack = inb(0x60);
    (void)ack;  // Ignore ACK for now
    
    vga_kbd[80*0 + 21] = 0x0F38;  // '8' (step 8)
    
    while (inb(0x64) & 0x01)
        (void)inb(0x60);
    
    vga_kbd[80*0 + 22] = 0x0F4F;  // 'O' (OK, done)
    vga_kbd[80*0 + 23] = 0x0F4B;  // 'K' (OK, done)
}

void kmain(uint32_t magic, void* mbi)
{
    (void)magic;  // Multiboot2 magic (0x36d76289)
    (void)mbi;    // Multiboot2 information structure pointer
    
    /* Debug: проверка, дошли ли мы до kmain */
    volatile uint16_t* vga_early = (volatile uint16_t*)0xB8000;
    vga_early[80*0 + 0] = 0x0F4B;  // 'K' (kmain)
    vga_early[80*0 + 1] = 0x0F4D;  // 'M' (kmain)
    vga_early[80*0 + 2] = 0x0F41;  // 'A' (kmain)
    vga_early[80*0 + 3] = 0x0F49;  // 'I' (kmain)
    vga_early[80*0 + 4] = 0x0F4E;  // 'N' (kmain)
    
    console_init();
    vga_early[80*0 + 5] = 0x0F43;  // 'C' (console)
    
    gdt_init();
    vga_early[80*0 + 6] = 0x0F47;  // 'G' (gdt)
    vga_early[80*0 + 7] = 0x0F47;  // 'G' (gdt done marker)
    
    /* Инициализация всей подсистемы прерываний */
    vga_early[80*0 + 8] = 0x0F49;  // 'I' (interrupts)
    vga_early[80*0 + 9] = 0x0F4E;  // 'N' (interrupts)
    vga_early[80*0 + 10] = 0x0F54;  // 'T' (interrupts)
    interrupts_subsystem_init();
    vga_early[80*0 + 11] = 0x0F4F;  // 'O' (interrupts done)
    vga_early[80*0 + 12] = 0x0F4B;  // 'K' (interrupts done)
    
    /* Инициализация PIT (100 Hz) */
    vga_early[80*0 + 15] = 0x0F54;  // 'T' (pit start)
    pit_init(100);
    vga_early[80*0 + 16] = 0x0F4F;  // 'O' (pit done)
    vga_early[80*0 + 17] = 0x0F52;  // 'R' (register timer start)
    
    /* Регистрация обработчика IRQ0 (PIT) */
    register_interrupt_handler(32, timer_handler);
    vga_early[80*0 + 18] = 0x0F52;  // 'R' (register timer done)
    
    /* Временно пропускаем kbd_init() - может зависать */
    /* kbd_init(); */
    vga_early[80*0 + 19] = 0x0F4B;  // 'K' (kbd skipped)
    vga_early[80*0 + 20] = 0x0F53;  // 'S' (skipped)
    vga_early[80*0 + 21] = 0x0F52;  // 'R' (register kbd start)
    
    /* Регистрация IRQ-обработчика для клавиатуры (IRQ1 -> IDT 33) */
    register_interrupt_handler(33, keyboard_handler);
    vga_early[80*0 + 22] = 0x0F52;  // 'R' (register kbd done)
    
    /* Debug: проверка регистрации обработчиков */
    volatile uint16_t* vga_reg = (volatile uint16_t*)0xB8000;
    vga_reg[80*14 + 0] = 0x0F52;  // 'R' (Registered)
    vga_reg[80*14 + 1] = 0x0F45;  // 'E' (Registered)
    vga_reg[80*14 + 2] = 0x0F47;  // 'G' (Registered)
    
    /* Замаскировать все IRQ */
    for (int i = 0; i < 16; i++) {
        pic_mask(i);
    }
    
    /* Размаскировать только IRQ0 (таймер) и IRQ1 (клавиатура) */
    pic_unmask(0);
    pic_unmask(1);
    
    /* Явно замаскировать IRQ:14 (ATA secondary) чтобы избежать spurious interrupts */
    pic_mask(14);
    
    /* Debug: проверить, что IRQ:14 замаскирован */
    volatile uint16_t* vga_mask = (volatile uint16_t*)0xB8000;
    vga_mask[80*11 + 0] = 0x0F4D;  // 'M' (Masked)
    vga_mask[80*11 + 1] = 0x0F41;  // 'A' (Masked)
    vga_mask[80*11 + 2] = 0x0F53;  // 'S' (Masked)
    vga_mask[80*11 + 3] = 0x0F4B;  // 'K' (Masked)
    
    vga_reg[80*14 + 3] = 0x0F55;  // 'U' (Unmasked)
    vga_reg[80*14 + 4] = 0x0F4E;  // 'N' (Unmasked)
    
    /* Включить прерывания */
    __asm__ volatile ("sti");
    
    vga_reg[80*14 + 5] = 0x0F53;  // 'S' (STI)
    vga_reg[80*14 + 6] = 0x0F54;  // 'T' (STI)
    vga_reg[80*14 + 7] = 0x0F49;  // 'I' (STI)
    
    vga_early[80*1 + 0] = 0x0F49;  // 'I' (ipc)
    /* Инициализация системы IPC */
    ipc_init();
    vga_early[80*1 + 1] = 0x0F43;  // 'C' (cap)
    
    /* Инициализация системы capabilities */
    capability_init();
    vga_early[80*1 + 2] = 0x0F53;  // 'S' (sched)
    
    /* Инициализация планировщика */
    scheduler_init();
    vga_early[80*1 + 3] = 0x0F50;  // 'P' (pci)
    
    /* Инициализация PCI */
    pci_init();
    vga_early[80*1 + 4] = 0x0F4E;  // 'N' (nx)
    
    /* Включение NX бита для защиты от выполнения данных */
    enable_nx_bit();
    vga_early[80*1 + 5] = 0x0F50;  // 'P' (pmm)
    
    /* Инициализация PMM (предполагаем 64MB памяти, начиная с 1MB) */
    if (pmm_init(0x100000, 0x4000000) != 0) /* 1MB - 64MB */
    {
        kputs("[PMM] Error: Failed to initialize PMM\n");
    }
    vga_early[80*1 + 6] = 0x0F50;  // 'P' (paging debug)
    
    /* Регистрация обработчика page fault для отладки */
    paging_debug_init();
    vga_early[80*1 + 7] = 0x0F50;  // 'P' (paging)
    
    /* Инициализация paging */
    if (paging_init() != 0)
    {
        kputs("[PAGING] Error: Failed to initialize paging\n");
    }
    else
    {
        /* ВАЖНО: После paging_init() нужно перерегистрировать обработчики,
         * так как они могут быть по виртуальным адресам */
        
        /* Перерегистрация обработчиков после paging */
        /* Теперь interrupt_handlers отображен в high-half, используем виртуальный адрес */
        register_interrupt_handler(32, timer_handler);
        register_interrupt_handler(33, keyboard_handler);
        
        /* Debug: проверка перерегистрации и значений обработчиков */
        volatile uint16_t* vga_test = (volatile uint16_t*)0xB8000;
        vga_test[80*13 + 0] = 0x0F52;  // 'R' (Re-registered)
        vga_test[80*13 + 1] = 0x0F45;  // 'E' (Re-registered)
        vga_test[80*13 + 2] = 0x0F52;  // 'R' (Re-registered)
        
        /* Debug: просто отметим, что перерегистрация прошла */
        vga_test[80*12 + 0] = 0x0F4F;  // 'O' (OK)
        vga_test[80*12 + 1] = 0x0F4B;  // 'K' (OK)
        
        /* Debug: просто отметим, что перерегистрация прошла */
        vga_test[80*12 + 0] = 0x0F4F;  // 'O' (OK)
        vga_test[80*12 + 1] = 0x0F4B;  // 'K' (OK)
        
        /* Инициализация VMM */
        vga_test[80*21 + 0] = 0x0F56;  // 'V' (VMM start)
        vga_test[80*21 + 1] = 0x0F4D;  // 'M' (VMM start)
        
        if (vmm_init() != 0)
        {
            kputs("[VMM] Error: Failed to initialize VMM\n");
        }
        
        vga_test[80*21 + 2] = 0x0F4F;  // 'O' (VMM OK)
        vga_test[80*21 + 3] = 0x0F4B;  // 'K' (VMM OK)
        
        /* Инициализация heap ядра */
        vga_test[80*21 + 4] = 0x0F48;  // 'H' (Heap start)
        vga_test[80*21 + 5] = 0x0F45;  // 'E' (Heap start)
        
        if (kernel_heap_init() != 0)
        {
            kputs("[HEAP] Error: Failed to initialize kernel heap\n");
        }
        
        vga_test[80*21 + 6] = 0x0F41;  // 'A' (Heap OK)
        vga_test[80*21 + 7] = 0x0F50;  // 'P' (Heap OK)
    }
    
    /* Debug: проверка, вышли ли мы из if */
    volatile uint16_t* vga_test = (volatile uint16_t*)0xB8000;
    vga_test[80*22 + 2] = 0x0F4F;  // 'O' (Out)
    vga_test[80*22 + 3] = 0x0F55;  // 'U' (Out)
    vga_test[80*22 + 4] = 0x0F54;  // 'T' (Out)
    
    /* Вывести приглашение при первом запуске */
    vga_test[80*22 + 5] = 0x0F50;  // 'P' (Prompt)
    vga_test[80*22 + 6] = 0x0F52;  // 'R' (Ready)
    
    /* Используем прямой VGA вывод для приглашения, чтобы обойти возможные проблемы с kputs() */
    const char* prompt = "rodnix> ";
    uint32_t prompt_pos = 0;
    
    /* Найти текущую позицию курсора (последняя строка) */
    #define VGA_WIDTH 80
    #define VGA_HEIGHT 25
    uint32_t last_line = VGA_HEIGHT - 1;
    uint32_t x = 0;
    
    /* Вывести символы приглашения напрямую в VGA */
    while (prompt[prompt_pos] != '\0' && x < VGA_WIDTH)
    {
        vga_test[last_line * VGA_WIDTH + x] = (uint16_t)prompt[prompt_pos] | (0x0F << 8);
        prompt_pos++;
        x++;
    }
    
    /* Обновить позицию курсора */
    uint16_t pos = last_line * VGA_WIDTH + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    
    /* Debug: проверка, прошла ли вывод */
    vga_test[80*22 + 7] = 0x0F4F;  // 'O' (OK)
    vga_test[80*22 + 8] = 0x0F4B;  // 'K' (OK)
    
    /* Основной цикл - обработка команд shell */
    for (;;)
    {
        __asm__ volatile ("hlt");
        
        /* Обработка готовой команды */
        if (shell_ready)
        {
            shell_ready = 0;
            
            /* Парсинг команды */
            if (shell_buffer[0] == '\0')
            {
                /* Пустая команда */
                kputs("rodnix> ");
            }
            else if (strcmp(shell_buffer, "help") == 0)
            {
                kputs("Available commands:\n");
                kputs("  help     - Show this help message\n");
                kputs("  clear    - Clear the screen\n");
                kputs("  echo     - Echo arguments\n");
                kputs("  info     - Show system information\n");
                kputs("  time     - Show system uptime\n");
                kputs("  meminfo  - Show memory information\n");
                kputs("rodnix> ");
            }
            else if (strcmp(shell_buffer, "clear") == 0)
            {
                console_init();
                kputs("rodnix> ");
            }
            else if (strncmp(shell_buffer, "echo ", 5) == 0)
            {
                kputs(shell_buffer + 5);
                kputc('\n');
                kputs("rodnix> ");
            }
            else if (strcmp(shell_buffer, "info") == 0)
            {
                kputs("RodNIX Kernel v0.1\n");
                kputs("Architecture: i386\n");
                kputs("Interrupts: Enabled\n");
                kputs("PIT frequency: 100 Hz\n");
                kputs("Timer ticks: ");
                kprint_dec(pit_get_ticks());
                kputs("\nUptime: ");
                kprint_dec(pit_get_time_ms());
                kputs(" ms\n");
                kputs("rodnix> ");
            }
            else if (strcmp(shell_buffer, "time") == 0)
            {
                kputs("Uptime: ");
                kprint_dec(pit_get_time_ms());
                kputs(" ms (");
                kprint_dec(pit_get_ticks());
                kputs(" ticks)\n");
                kputs("rodnix> ");
            }
            else if (strcmp(shell_buffer, "meminfo") == 0)
            {
                kputs("Memory Information:\n");
                kputs("==================\n");
                kputs("Physical Memory:\n");
                kputs("  Total pages: ");
                kprint_dec(pmm_get_total_pages());
                kputs("\n  Free pages: ");
                kprint_dec(pmm_get_free_pages());
                kputs("\n  Used pages: ");
                kprint_dec(pmm_get_used_pages());
                kputs("\n  Total: ");
                kprint_dec((pmm_get_total_pages() * PAGE_SIZE) / 1024);
                kputs(" KB\n  Free: ");
                kprint_dec((pmm_get_free_pages() * PAGE_SIZE) / 1024);
                kputs(" KB\n  Used: ");
                kprint_dec((pmm_get_used_pages() * PAGE_SIZE) / 1024);
                kputs(" KB\n");
                kputs("Virtual Memory:\n");
                kputs("  Total: ");
                kprint_dec(vmm_get_total_memory() / 1024);
                kputs(" KB\n  Free: ");
                kprint_dec(vmm_get_free_memory() / 1024);
                kputs(" KB\n  Used: ");
                kprint_dec(vmm_get_used_memory() / 1024);
                kputs(" KB\n");
                kputs("Kernel Heap:\n");
                kputs("  Total: ");
                kprint_dec(heap_get_total_size(&kernel_heap) / 1024);
                kputs(" KB\n  Free: ");
                kprint_dec(heap_get_free_size(&kernel_heap) / 1024);
                kputs(" KB\n  Used: ");
                kprint_dec(heap_get_used_size(&kernel_heap) / 1024);
                kputs(" KB\n");
                kputs("rodnix> ");
            }
            else
            {
                kputs("Unknown command: ");
                kputs(shell_buffer);
                kputs("\nType 'help' for available commands.\n");
                kputs("rodnix> ");
            }
            
            /* Очистить буфер */
            shell_buffer_pos = 0;
            shell_buffer[0] = '\0';
        }
    }
}


