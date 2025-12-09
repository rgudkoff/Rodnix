#include "../include/console.h"
#include "../include/ports.h"
#include "../include/gdt.h"
#include "../include/idt.h"
#include "../include/isr.h"
#include "../include/pic.h"
#include "../include/pit.h"
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
    
    /* Инициализация системы IPC */
    kputs("[INIT] Initializing IPC system...\n");
    ipc_init();
    kputs("[INIT] IPC system initialized.\n\n");
    
    /* Инициализация системы capabilities */
    kputs("[INIT] Initializing capability system...\n");
    capability_init();
    kputs("[INIT] Capability system initialized.\n\n");
    
    /* Инициализация планировщика */
    kputs("[INIT] Initializing scheduler...\n");
    scheduler_init();
    kputs("[INIT] Scheduler initialized.\n\n");
    
    /* Инициализация PCI */
    kputs("[INIT] Initializing PCI...\n");
    pci_init();
    kputs("[INIT] PCI initialized.\n\n");
    
    /* Включение NX бита для защиты от выполнения данных */
    kputs("[INIT] Enabling NX bit...\n");
    enable_nx_bit();
    kputs("[INIT] NX bit enabled.\n\n");
    
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
    
    /* Регистрация обработчика page fault для отладки */
    paging_debug_init();
    kputs("[INIT] Page fault handler registered.\n\n");
    
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
        kputs("[INIT] Creating identity mapping for first 4MB...\n");
        kputs("[INIT] This will map virtual addresses 0x0-0x3FFFFF to physical 0x0-0x3FFFFF\n");
        uint32_t mapped_count = 0;
        uint32_t failed_count = 0;
        for (uint32_t i = 0; i < 1024; i++) /* 1024 страницы = 4MB */
        {
            uint32_t addr = i * PAGE_SIZE;
            if (paging_map_page(addr, addr, PAGE_KERNEL) != 0)
            {
                kputs("[INIT] ERROR: Failed to map page at 0x");
                kprint_hex(addr);
                kputs("\n");
                failed_count++;
            }
            else
            {
                mapped_count++;
            }
            
            /* Выводим прогресс каждые 256 страниц */
            if ((i + 1) % 256 == 0)
            {
                kputs("[INIT] Mapped ");
                kprint_dec(i + 1);
                kputs(" / 1024 pages...\n");
            }
        }
        kputs("[INIT] Identity mapping complete: ");
        kprint_dec(mapped_count);
        kputs(" pages mapped, ");
        kprint_dec(failed_count);
        kputs(" failed\n");
        
        /* Убеждаемся, что IDT отображен в identity mapping */
        kputs("[INIT] Ensuring IDT is identity mapped...\n");
        extern struct idt_entry idt[];
        uint32_t idt_addr = (uint32_t)&idt;
        uint32_t idt_phys = paging_get_physical(idt_addr);
        if (idt_phys != idt_addr && idt_addr < 0x400000)
        {
            kputs("[INIT] Mapping IDT to identity...\n");
            paging_map_page(idt_addr, idt_addr, PAGE_KERNEL);
        }
        
        /* Убеждаемся, что обработчики прерываний отображены */
        kputs("[INIT] Ensuring interrupt handlers are identity mapped...\n");
        extern void isr14();
        uint32_t isr14_addr = (uint32_t)isr14;
        uint32_t isr14_phys = paging_get_physical(isr14_addr);
        if (isr14_phys != isr14_addr && isr14_addr < 0x400000)
        {
            kputs("[INIT] Mapping ISR14 to identity...\n");
            paging_map_page(isr14_addr, isr14_addr, PAGE_KERNEL);
        }
        
        /* Убеждаемся, что GDT отображен в identity mapping */
        kputs("[INIT] Ensuring GDT is identity mapped...\n");
        extern struct gdt_entry gdt[];
        uint32_t gdt_addr = (uint32_t)&gdt;
        uint32_t gdt_phys = paging_get_physical(gdt_addr);
        if (gdt_phys != gdt_addr && gdt_addr < 0x400000)
        {
            kputs("[INIT] Mapping GDT to identity...\n");
            paging_map_page(gdt_addr, gdt_addr, PAGE_KERNEL);
        }
        
        /* Убеждаемся, что функции console отображены в identity mapping */
        kputs("[INIT] Ensuring console functions are identity mapped...\n");
        extern void kputs(const char* s);
        extern void kprint_hex(uint32_t v);
        uint32_t kputs_addr = (uint32_t)kputs;
        uint32_t kprint_hex_addr = (uint32_t)kprint_hex;
        
        uint32_t kputs_phys = paging_get_physical(kputs_addr);
        if (kputs_phys != kputs_addr && kputs_addr < 0x400000)
        {
            kputs("[INIT] Mapping kputs to identity...\n");
            paging_map_page(kputs_addr, kputs_addr, PAGE_KERNEL);
        }
        
        uint32_t kprint_hex_phys = paging_get_physical(kprint_hex_addr);
        if (kprint_hex_phys != kprint_hex_addr && kprint_hex_addr < 0x400000)
        {
            kputs("[INIT] Mapping kprint_hex to identity...\n");
            paging_map_page(kprint_hex_addr, kprint_hex_addr, PAGE_KERNEL);
        }
        
        /* КРИТИЧЕСКИ ВАЖНО: Убеждаемся, что все page tables отображены в identity mapping */
        /* После включения пейджинга процессор должен иметь доступ к page tables */
        kputs("[INIT] Ensuring all page tables are identity mapped...\n");
        uint32_t* page_dir = paging_get_directory();
        uint32_t page_tables_mapped = 0;
        for (uint32_t i = 0; i < 1024; i++)
        {
            pde_t* pde = (pde_t*)&page_dir[i];
            if (pde->present)
            {
                uint32_t table_phys = FRAME_ADDR(pde->frame);
                /* Если page table находится в первых 4MB, отображаем ее в identity mapping */
                if (table_phys < 0x400000)
                {
                    uint32_t table_virt = table_phys;
                    uint32_t mapped_phys = paging_get_physical(table_virt);
                    if (mapped_phys != table_phys)
                    {
                        kputs("[INIT] Mapping page table at 0x");
                        kprint_hex(table_phys);
                        kputs(" to identity...\n");
                        paging_map_page(table_virt, table_phys, PAGE_KERNEL);
                        page_tables_mapped++;
                    }
                }
            }
        }
        kputs("[INIT] Page tables mapping complete: ");
        kprint_dec(page_tables_mapped);
        kputs(" page tables mapped to identity\n");
        
        /* Отображаем ядро в виртуальную память (0xC0000000) */
        /* Ядро загружено по адресу 1MB, отображаем его в 0xC0100000 */
        kputs("[INIT] Mapping kernel to virtual memory...\n");
        kputs("[INIT] Mapping physical 0x100000-0x4FFFFF to virtual 0xC0100000-0xC04FFFFF\n");
        uint32_t kernel_mapped = 0;
        uint32_t kernel_failed = 0;
        for (uint32_t i = 0; i < 1024; i++) /* 1024 страницы = 4MB */
        {
            uint32_t virt = 0xC0100000 + i * PAGE_SIZE; /* Kernel virtual base (3GB + 1MB) */
            uint32_t phys = 0x100000 + i * PAGE_SIZE;   /* Физический адрес ядра (1MB) */
            if (paging_map_page(virt, phys, PAGE_KERNEL) != 0)
            {
                kputs("[INIT] ERROR: Failed to map kernel page virt=0x");
                kprint_hex(virt);
                kputs(" phys=0x");
                kprint_hex(phys);
                kputs("\n");
                kernel_failed++;
            }
            else
            {
                kernel_mapped++;
            }
        }
        kputs("[INIT] Kernel mapping complete: ");
        kprint_dec(kernel_mapped);
        kputs(" pages mapped, ");
        kprint_dec(kernel_failed);
        kputs(" failed\n");
        
        /* Включаем paging */
        kputs("[INIT] Enabling paging...\n");
        kputs("[INIT] Current page directory: 0x");
        kprint_hex((uint32_t)paging_get_directory());
        kputs("\n");
        
        /* Проверяем identity mapping перед включением */
        kputs("[INIT] Testing identity mapping...\n");
        uint32_t test_addrs[] = {0x0, 0x1000, 0xB8000, 0x100000};
        for (int i = 0; i < 4; i++)
        {
            uint32_t phys = paging_get_physical(test_addrs[i]);
            kputs("  Addr 0x");
            kprint_hex(test_addrs[i]);
            kputs(" -> Phys 0x");
            kprint_hex(phys);
            kputs(phys == test_addrs[i] ? " [OK]\n" : " [FAIL]\n");
        }
        
        /* Проверяем, что page directory отображен в identity mapping */
        uint32_t page_dir_addr = (uint32_t)paging_get_directory();
        uint32_t page_dir_phys;
        if (page_dir_addr >= 0xC0000000)
        {
            page_dir_phys = VIRT_TO_PHYS(page_dir_addr);
        }
        else
        {
            page_dir_phys = page_dir_addr;
        }
        kputs("[INIT] Page directory virtual: 0x");
        kprint_hex(page_dir_addr);
        kputs(", physical: 0x");
        kprint_hex(page_dir_phys);
        kputs("\n");
        
        /* Убеждаемся, что page directory отображен в identity mapping */
        /* Это нужно для того, чтобы к нему можно было обращаться после включения пейджинга */
        if (page_dir_phys >= 0x400000)
        {
            kputs("[INIT] WARNING: Page directory is above 4MB, may cause issues!\n");
        }
        else
        {
            /* Проверяем, что page directory отображен */
            uint32_t mapped_phys = paging_get_physical(page_dir_phys);
            if (mapped_phys != page_dir_phys)
            {
                kputs("[INIT] Mapping page directory to identity...\n");
                paging_map_page(page_dir_phys, page_dir_phys, PAGE_KERNEL);
            }
        }
        
        kputs("[INIT] ========================================\n");
        kputs("[INIT] About to enable paging...\n");
        kputs("[INIT] ========================================\n");
        
        /* Получаем текущий EIP для отладки */
        uint32_t current_eip;
        __asm__ volatile ("call 1f\n\t"
                         "1: pop %0"
                         : "=r"(current_eip));
        kputs("[INIT] Current code location (EIP): 0x");
        kprint_hex(current_eip);
        kputs("\n");
        
        /* Проверяем, что текущий код отображен */
        uint32_t code_phys = paging_get_physical(current_eip);
        kputs("[INIT] Code physical address: 0x");
        kprint_hex(code_phys);
        kputs(code_phys == current_eip ? " [OK - identity mapped]\n" : " [WARNING - not identity mapped]\n");
        
        /* Получаем текущий ESP для отладки */
        uint32_t current_esp;
        __asm__ volatile ("mov %%esp, %0" : "=r"(current_esp));
        kputs("[INIT] Current stack pointer (ESP): 0x");
        kprint_hex(current_esp);
        kputs("\n");
        
        /* Проверяем, что стек отображен */
        uint32_t stack_phys = paging_get_physical(current_esp);
        kputs("[INIT] Stack physical address: 0x");
        kprint_hex(stack_phys);
        kputs(stack_phys == current_esp ? " [OK - identity mapped]\n" : " [WARNING - not identity mapped]\n");
        
        kputs("[INIT] Calling paging_enable()...\n");
        paging_enable();
        
        kputs("[INIT] ========================================\n");
        kputs("[INIT] Returned from paging_enable()!\n");
        kputs("[INIT] ========================================\n");
        
        /* После включения paging, kputs должен работать через identity mapping */
        /* Если произойдет page fault, обработчик выведет информацию */
        kputs("[INIT] Paging enabled successfully.\n");
        
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


