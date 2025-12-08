# RodNIX

Минимальная учебная ОС под i386 (GRUB2 multiboot2). Цель — пошагово собрать микрокернел: GDT/IDT, обработчики прерываний, таймер PIT, клавиатура, менеджер памяти и т.д.

## Структура
- `boot/` — точка входа, multiboot2, стеки, stubs GDT/IDT/ISR.
- `kernel/` — ядро: GDT/IDT/ISR/IRQ/PIC/PIT/keyboard, kmain.
- `drivers/` — простые драйверы VGA-консоли и портов ввода-вывода.
- `include/` — заголовки.
- `link.ld` — линковка под `elf32-i386`, загрузка по 1 МиБ.
- `grub.cfg` — меню для ISO.
- `Makefile` — сборка ядра, ISO, запуск в QEMU.

## Требования
- `i686-elf-gcc`, `i686-elf-ld`
- `nasm`
- `grub-mkrescue`, `xorriso`
- `qemu-system-i386`

Убедитесь, что инструменты доступны в PATH. На macOS удобно ставить через `brew install i686-elf-gcc nasm xorriso qemu` и `brew install --cask gcc@<верс>`, затем добавить `i686-elf-*` в PATH.

## Сборка
```sh
cd /Users/romangudkov/Rodnix
make
```
Результат: `build/rodnix.kernel`.

## ISO и запуск
```sh
make run          # собирает ISO и запускает QEMU (-serial stdio)
```
Файл ISO: `rodnix.iso`, размещение ядра в `iso/boot/rodnix.kernel`.

## Отладка
```sh
make debug        # QEMU -s -S, далее
gdb build/rodnix.kernel
```

## Текущее состояние
- Multiboot2 заголовок, freestanding 32-bit.
- GDT/IDT/ISR/IRQ, PIC ремап на 0x20/0x28.
- PIT 100 Гц (счётчик тиков).
- Обработчик клавиатуры (keymap, вывод символов).
- VGA-консоль 80x25.
- **Система управления устройствами (Device Manager)**:
  - Регистрация и поиск устройств по имени/типу
  - Унифицированный интерфейс для работы с устройствами
  - Состояния устройств (uninitialized, initialized, ready, error, offline)
- **Драйвер ATA/IDE**:
  - Поддержка чтения/записи секторов
  - Инициализация и определение дисков
  - Работа с PRIMARY каналом (master/slave)
- **VFS (Virtual File System)**:
  - Абстракция файловой системы
  - Поддержка монтирования/размонтирования
  - Интерфейсы для работы с файлами и директориями
- **Shell команды**:
  - `devices` - список зарегистрированных устройств
  - `meminfo` - информация о памяти (физическая, виртуальная, heap)
- **Менеджер памяти**:
  - **PMM (Physical Memory Manager)**: управление физическими страницами через bitmap
  - **Paging**: страничная адресация с поддержкой page directory и page tables
  - **VMM (Virtual Memory Manager)**: управление виртуальными адресами
  - **Heap Allocator**: динамическое выделение памяти для ядра (kmalloc/kfree/krealloc)
  - Отображение ядра в виртуальную память по адресу 0xC0000000

## Планы
- Реализация простой файловой системы (initrd или простой формат)
- Получение информации о памяти из Multiboot2
- Расширение VMM для поддержки процессов
- Расширение VFS для работы с реальными файловыми системами

