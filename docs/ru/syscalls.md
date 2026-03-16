# Системные вызовы

## Стратегия

- Таблица системных вызовов располагается в POSIX‑слое.
- Вход из userland через ловушку/переходник (traps).
- Пользовательские API строятся поверх POSIX‑слоя.
- Для userland ABI принимаем vendor-baseline канон для ключевых числовых
  констант (`errno/fcntl/wait`) с автоматической проверкой на этапе сборки.

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
- Добавлен `POSIX_SYS_EXIT` (wake joiner + завершение user thread).
- Добавлены `MMAP/MUNMAP/BRK` (минимальный VM v1: anonymous private regions, lazy allocation на page fault).
- Диспетчер сначала обслуживает `SYS_NOP` (legacy), затем POSIX namespace, чтобы избежать конфликтов номеров.
- Добавлена базовая валидация user pointers/ranges для `OPEN/READ/WRITE/UNAME`.
- Неизвестный номер возвращает `RDNX_E_UNSUPPORTED`.
- Временная модель: `open` возвращает fd из per‑task таблицы (простая фиксированная таблица).

### Семантика `SYS_NOP`

`SYS_NOP` в RodNIX v1 трактуется как **cooperative yield point** для userland
polling-циклов: вызов может уступить квант планировщику (`scheduler_yield`),
давая прогресс другим runnable-потокам (child/reaper/wakeup-path).

Это контрактное поведение, а не "строго пустой" syscall.

### UNAME

- Syscall: `POSIX_SYS_UNAME`.
- Заполняет `utsname_t`:
  - `hdr` — ABI header (`abi_version`, `size`).
  - `sysname` — имя ядра.
  - `nodename` — имя узла (пока статически).
  - `release` — версия релиза.
  - `version` — строка сборки.
  - `machine` — архитектура.
- Возврат: `RDNX_OK` или `RDNX_E_INVALID`.

## Инварианты

- Любой syscall должен быть описан и иметь стабильный номер.
- Обработчик syscalls не должен зависеть от конкретного драйвера.
- Публичный userspace ABI использует только `POSIX_SYS_*`.
- `SYS_*` остаётся как legacy‑пространство для совместимости и не должен
  пересекаться с активным POSIX диапазоном.

## Таблица POSIX‑syscalls (фиксированные номера)

Формат: `номер — имя — статус`

- `0` — `NOSYS` — stable
- `1` — `GETPID` — stable
- `2` — `GETUID` — stable
- `3` — `GETEUID` — stable
- `4` — `GETGID` — stable
- `5` — `GETEGID` — stable
- `6` — `SETUID` — stable
- `7` — `SETEUID` — stable
- `8` — `SETGID` — stable
- `9` — `SETEGID` — stable
- `10` — `OPEN` — experimental
- `11` — `CLOSE` — experimental
- `12` — `READ` — experimental
- `13` — `WRITE` — experimental
- `14` — `UNAME` — stable
- `15` — `EXIT` — experimental
- `16` — `EXEC` — experimental
- `17` — `SPAWN` — experimental
- `18` — `WAITPID` — experimental
- `19` — `READDIR` — experimental
- `20` — `FCNTL` — experimental
- `21` — `NETIFLIST` — experimental
- `22` — `MMAP` — experimental
- `23` — `MUNMAP` — experimental
- `24` — `BRK` — experimental
- `25` — `FORK` — experimental

Свободные номера помечаются как `RESERVED` и не переиспользуются.

## Где смотреть в коде

- `kernel/common/syscall.c`, `kernel/common/syscall.h`.
- `kernel/arch/x86_64/idt.c` (IDT entry 0x80).
- `kernel/arch/x86_64/isr_handlers.c` (dispatch).
- `userland/include/sys/{errno.h,fcntl.h,wait.h}` (userland ABI constants).
- `third_party/bsd/*/sys/sys/{errno.h,fcntl.h,wait.h}` (канон).
- `scripts/check_bsd_abi_headers.py` + `userland/Makefile` (`check-bsd-abi`).
