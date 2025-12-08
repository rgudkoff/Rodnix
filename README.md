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

## Планы
- Маски прерываний и управление приоритетами.
- Менеджер физической и виртуальной памяти, paging.
- Heap/allocator для ядра.
- VFS и базовые драйверы устройств.

