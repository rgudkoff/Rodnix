#include "../include/console.h"
#include "../include/ports.h"
#include "../include/gdt.h"
#include "../include/idt.h"
#include "../include/isr.h"
#include "../include/pic.h"
#include "../include/pit.h"
#include "../include/device.h"
#include "../include/vfs.h"
#include "../include/ata.h"
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

/* IRQ-обработчик таймера (IRQ0) - вызывает PIT handler */
static void timer_handler(struct registers* regs)
{
    (void)regs;
    pit_handler();
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
        kputc(ch);
    }
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
    
    /* Инициализация PIT (100 Hz) */
    kputs("[INIT] Initializing PIT (100 Hz)...\n");
    pit_init(100);
    kputs("[INIT] PIT initialized.\n\n");
    
    /* Регистрация обработчика IRQ0 (PIT) */
    kputs("[INIT] Registering timer IRQ handler (IRQ0)...\n");
    register_interrupt_handler(32, timer_handler);
    
    kbd_init();
    
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
    
    /* Инициализация системы устройств */
    kputs("[INIT] Initializing device manager...\n");
    kputs("[INIT] Device manager initialized.\n\n");
    
    /* Регистрация ATA устройств */
    kputs("[INIT] Registering ATA devices...\n");
    ata_register_devices();
    kputs("[INIT] ATA devices registered.\n\n");
    
    /* Инициализация менеджера памяти */
    kputs("[INIT] Initializing memory manager...\n");
    
    /* Инициализация PMM (предполагаем 64MB памяти, начиная с 1MB) */
    kputs("[INIT] Initializing PMM...\n");
    if (pmm_init(0x100000, 0x4000000) != 0) /* 1MB - 64MB */
    {
        kputs("[INIT] PMM initialization failed!\n");
    }
    else
    {
        kputs("[INIT] PMM initialized.\n");
    }
    
    /* Инициализация paging */
    kputs("[INIT] Initializing paging...\n");
    if (paging_init() != 0)
    {
        kputs("[INIT] Paging initialization failed!\n");
    }
    else
    {
        kputs("[INIT] Paging initialized.\n");
        
        /* Identity mapping для первых 4MB (чтобы код продолжал работать после включения paging) */
        kputs("[INIT] Creating identity mapping...\n");
        for (uint32_t i = 0; i < 1024; i++) /* 1024 страницы = 4MB */
        {
            uint32_t addr = i * PAGE_SIZE;
            paging_map_page(addr, addr, PAGE_KERNEL);
        }
        kputs("[INIT] Identity mapping created.\n");
        
        /* Отображаем ядро в виртуальную память (0xC0000000) */
        /* Ядро загружено по адресу 1MB, отображаем его в 0xC0100000 */
        kputs("[INIT] Mapping kernel to virtual memory...\n");
        for (uint32_t i = 0; i < 1024; i++) /* 1024 страницы = 4MB */
        {
            uint32_t virt = 0xC0100000 + i * PAGE_SIZE; /* Kernel virtual base (3GB + 1MB) */
            uint32_t phys = 0x100000 + i * PAGE_SIZE;   /* Физический адрес ядра (1MB) */
            paging_map_page(virt, phys, PAGE_KERNEL);
        }
        kputs("[INIT] Kernel memory mapped.\n");
        
        /* Включаем paging */
        paging_enable();
        
        /* Инициализация VMM */
        kputs("[INIT] Initializing VMM...\n");
        vmm_init();
        kputs("[INIT] VMM initialized.\n");
        
        /* Инициализация heap ядра */
        kputs("[INIT] Initializing kernel heap...\n");
        if (kernel_heap_init() != 0)
        {
            kputs("[INIT] Kernel heap initialization failed!\n");
        }
        else
        {
            kputs("[INIT] Kernel heap initialized.\n");
        }
    }
    kputs("\n");
    
    /* Инициализация VFS */
    kputs("[INIT] Initializing VFS...\n");
    vfs_init();
    kputs("[INIT] VFS initialized.\n\n");
    
    kputs("[INIT] System ready. Interrupts enabled.\n");
    kputs("RodNIX Shell v0.2\n");
    kputs("Type 'help' for available commands.\n\n");
    kputs("rodnix> ");
    
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
                kputs("  devices  - List all registered devices\n");
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
            else if (strcmp(shell_buffer, "devices") == 0)
            {
                device_list_all();
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


