# ARM64 - Реализация для архитектуры ARM64

Реализация ядра для процессоров ARM 64-бит (RISC архитектура).

## Особенности

- **RISC архитектура**: Reduced Instruction Set Computer
- **64-битный режим**: AArch64
- **4-уровневая страничная структура**: Level 0 → Level 1 → Level 2 → Level 3
- **EL (Exception Levels)**: Уровни привилегий

## Компоненты

- `config.h` - конфигурация ARM64
- `types.h` - типы данных ARM64
- `interrupts.c` - обработка прерываний (IRQ/FIQ)
- `memory.c` - управление памятью (MMU)
- `cpu.c` - работа с процессором
- `boot.S` - код загрузки

## Регистры

ARM64 использует 31 64-битных регистра общего назначения:
- x0-x30 (x30 = LR, Link Register)
- SP (Stack Pointer)
- PC (Program Counter)

## Страничная структура

```
Level 0 (512 entries) → Level 1 (512 entries) → Level 2 (512 entries) → Level 3 (512 entries)
```

Размеры страниц:
- 4KB (обычные страницы)
- 2MB (large pages)
- 1GB (huge pages)

## Exception Levels

- EL0: User mode
- EL1: Kernel mode
- EL2: Hypervisor
- EL3: Secure monitor

