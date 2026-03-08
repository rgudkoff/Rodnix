# Различия: документация vs реализация (на текущий момент)

Документ фиксирует расхождения между описанной архитектурой и фактической
реализацией в коде. Цель — дать точный список "что уже есть" и "чего еще
нет", чтобы планирование и разработка были синхронизированы.

## Планировщик

Документация:
- Требуется приоритетный планировщик + таймслайс (MLQ/priority queues).
- Целевая эволюция: иерархическая модель `bucket -> thread_group -> thread`,
  QoS-aware вытеснение и anti-starvation окна (см. `docs/ru/scheduler.md`).

Реализация:
- `kernel/common/scheduler/` — модульный планировщик (state/runqueue/control/tick/switch/reaper/debug).
- Приоритеты используются для выбора очереди.
- Политики CFS/FIFO/PRIORITY в enum, реализованы RR/FIFO/PRIORITY на базовом уровне.

Разница:
- Классы `SCHED_CLASS_TIMESHARE/REALTIME` есть; квант зависит от приоритета и CPU‑bound (упрощённая политика).
- Динамика приоритетов реализована (boost при пробуждении, penalty по `sched_usage`) только для TIMESHARE, без тонкой настройки и разных кривых.
- IPC‑наследование приоритетов есть, но best‑effort (стек глубиной 4, без сложных сценариев и анализа цепочек зависимостей).
- Нет уровня QoS bucket scheduling (EDF-like/budget).
- Нет уровня fairness по `thread_group`.
- Нет `warp window`/`starvation avoidance window` и связанных SLA-метрик.

## Таймерный тик (IRQ32)

Документация:
- Таймерный тик — единый источник преэмпции (один tick на IRQ).

Реализация:
- `scheduler_tick()` вызывается только в `interrupt_dispatch()` при
  `vector == 32`, затем выполняется `scheduler_switch_from_irq()`.

Разница:
- Расхождений нет (один тик на прерывание).

## IPC и порты

Документация:
- Сообщения фикс/перем длины.
- Передача портов в сообщениях.
- Граница слоев через IDL + автогенерацию.

Реализация:
- `kernel/common/ipc.c` — базовые таблицы портов и очереди сообщений.
- `ipc_message_t` — фиксированный буфер 4096 байт.
- Реализованы port_set (массив портов), простые send/receive без блокировок.
- Есть перенос портов в сообщениях (`ports[]`, `port_count`) с увеличением refcount, но без модели прав/namespace.

Разница:
- Есть минимальная проверка прав (send/receive) без per-task namespace; права глобальные, модель неполная.
- Нет peek и продвинутых политик очередей.
- IDL пока не подключён к IPC/runtime (нет автосгенерированных client/server C‑стабов).

## Userland и bootstrap

Документация:
- Bootstrap‑порт и bootstrap‑сервер в userland (раздача портов сервисов).

Реализация:
- Добавлен каркас userland в `userland/` и скелет bootstrap‑сервера.
- Есть минимальный запуск userland: `run /bin/init` из shell загружает ELF64 и уходит в ring3.
- В ядре зарезервирован bootstrap‑порт (placeholder).
- Есть временный kernel‑mode bootstrap server (thread).
- Есть loader ELF64 ET_EXEC + переключение CR3 на user PML4.
- Есть ring3-инфраструктура (GDT user‑сегменты + TSS RSP0) и syscall trap `int 0x80`.

Разница:
- Нет полноценной process model (fork/exec/wait, изоляция процессов, lifecycle).
- Нет userland bootstrap‑сервера и протокола раздачи портов (временно работает kernel‑mode bootstrap thread).
- Нет зрелой модели безопасности/изоляции для userland и стабильного userspace ABI.

## Системные вызовы (BSD‑слой)

Документация:
- Таблица syscalls в BSD‑слое.
- Вход через traps из userland.

Реализация:
- Минимальная таблица syscalls в `kernel/common/syscall.c`.
- Вектор `0x80` подключён в IDT как trap gate (DPL=3).
- Есть базовый dispatch по ABI (`rax` номер, `rdi..r9` аргументы).
- Есть каркас POSIX‑слоя (`kernel/posix/posix_syscall.c`).
- Реализованы `SYS_NOP`, `POSIX_SYS_NOSYS`, `GETPID/GETUID/GETEUID/GETGID/GETEGID`.
- Добавлены базовые `SETUID/SETEUID/SETGID/SETEGID` с проверкой root.
- Добавлены `OPEN/CLOSE/READ/WRITE` на базе VFS.

Разница:
- Нет полноценной таблицы syscalls и набора вызовов.
- Userland есть только в минимальном профиле (один ELF, базовые вызовы, без POSIX process semantics).
 - Есть минимальная per‑task fd‑таблица, но нет полноценного POSIX‑семантического поведения (dup, cloexec, права и т.п.).

## VFS

Документация:
- Модель vnode/inode.
- Name cache.
- Монтирование.
- Initrd/ramfs для старта.

Реализация:
- `kernel/fs/vfs.c` — VFS-скелет с vnode/inode и RAMFS.
- Есть mount-структура и возможность подключать ramfs на путь.
- Есть name cache с инвалидацией по генерации.
 - Есть импорт initrd по простому формату `RDNX` (таблица файлов).
 - Есть `vfs_mount_initrd_root()` для замены root на initrd‑RAMFS.

Разница:
- Нет полноценного VFS с несколькими типами ФС и драйверами.
- Нет полноценного name cache (только грубая инвалидация).
 - Нет полноценного initrd (cpio/tar); root‑mount пока только через RAMFS и простой формат `RDNX`.
 - Initrd подключён из boot‑модуля, но формат остаётся `RDNX` (не cpio/tar).

## Сеть

Документация:
- Позже: BSD‑сокеты (AF_INET), порядок loopback → UDP → TCP.

Реализация:
- Есть минимальный loopback (очередь пакетов в памяти).
- Есть базовые UDP‑сокеты (AF_INET + SOCK_DGRAM) для loopback.

Разница:
- Нет полноценного сетевого стека, реального TCP и драйверов сетевых устройств.

## Учёт и права

Документация:
- Реальные/эффективные UID/GID.
- Базовая модель прав, позже MAC‑крючки.

Реализация:
- В `task_t` добавлены реальные/эффективные UID/GID, default = 0.
- Есть минимальный security‑stub (`security_check_euid`).

Разница:
- Нет модели прав и проверок на уровне syscalls (кроме базового `security_check_euid`).
- Нет MAC‑крючков.

## IDL и генерация

Документация:
- `.defs` + генератор клиент/серверных стабов и диспетчеров.

Реализация:
 - Добавлен генератор `scripts/idl/idlgen.py` (headers + client/server `.c`).
 - Добавлен минимальный runtime (`kernel/common/idl_runtime.c`) для IPC‑вызовов.

Разница:
 - Интеграция с IPC/runtime минимальная (fixed‑size payload, без userland ABI).
 - Нет генерации маршаллинга для variable‑size сообщений и прав на порты.
