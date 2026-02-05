# Системные вызовы

## Стратегия

- Таблица системных вызовов располагается в POSIX‑слое.
- Вход из userland через ловушку/переходник (traps).
- Пользовательские API строятся поверх POSIX‑слоя.

## MVP

- Минимальный набор вызовов для запуска userland.
- Явная валидация аргументов и прав.

## Текущая реализация (минимальный каркас)

- Вектор ловушки: `0x80` (IDT trap gate с DPL=3).
- ABI (x86_64, базовый):
  - `rax` — номер syscall.
  - `rdi, rsi, rdx, r10, r8, r9` — аргументы 1..6.
  - `rax` — код возврата.
- Таблица syscalls: `kernel/common/syscall.c`.
- Есть POSIX‑слой: `kernel/posix/posix_syscall.c` (таблица + минимальные вызовы).
- Реализованы `SYS_NOP`, `POSIX_SYS_NOSYS`, `GETPID/GETUID/GETEUID/GETGID/GETEGID`, `UNAME`.
- Реализованы базовые `SETUID/SETEUID/SETGID/SETEGID` (только для root).
- Добавлены `OPEN/CLOSE/READ/WRITE` поверх VFS (in‑kernel, без userland).
- Неизвестный номер возвращает `RDNX_E_UNSUPPORTED`.
- Временная модель: `open` возвращает fd из per‑task таблицы (простая фиксированная таблица).

### UNAME

- Syscall: `POSIX_SYS_UNAME`.
- Заполняет `utsname_t`:
  - `sysname` — имя ядра.
  - `nodename` — имя узла (пока статически).
  - `release` — версия релиза.
  - `version` — строка сборки.
  - `machine` — архитектура.
- Возврат: `RDNX_OK` или `RDNX_E_INVALID`.

## Инварианты

- Любой syscall должен быть описан и иметь стабильный номер.
- Обработчик syscalls не должен зависеть от конкретного драйвера.

## Где смотреть в коде

- `kernel/common/syscall.c`, `kernel/common/syscall.h`.
- `kernel/arch/x86_64/idt.c` (IDT entry 0x80).
- `kernel/arch/x86_64/isr_handlers.c` (dispatch).
