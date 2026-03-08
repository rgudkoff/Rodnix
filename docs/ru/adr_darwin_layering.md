# ADR: Darwin-Style Layering For RodNIX

Статус: accepted  
Дата: 2026-03-08

## Контекст

RodNIX уже имеет рабочий POSIX syscall слой, но в нём смешаны:

- ABI-адаптер (`kernel/posix/*`)
- Unix-семантика процессов (`spawn/exit/waitpid`)
- доступ к низкоуровневым механизмам ядра

Такое смешение усложняет развитие ядра: изменения в scheduler/VM/VFS быстро
протекают в POSIX ABI и увеличивают риск регрессий.

## Решение

Принять целевую архитектуру в стиле Darwin:

1. `Kernel mechanisms`  
   Планировщик, память, VFS, IPC, безопасность, примитивы синхронизации.
2. `Unix compatibility layer` (`kernel/unix/*`)  
   Unix-модель процессов/FD/ожидания/сигналов поверх механизмов ядра.
3. `POSIX export layer` (`kernel/posix/*`)  
   Тонкий ABI-адаптер системных вызовов к Unix-слою.

## Первый практический шаг (в этом изменении)

- Добавлен `kernel/unix/unix_layer.h`.
- Добавлен `kernel/unix/unix_process.c`.
- В `kernel/posix/posix_syscall.c` системные вызовы:
  - `posix_exit` -> `unix_proc_exit`
  - `posix_spawn` -> `unix_proc_spawn`
  - `posix_waitpid` -> `unix_proc_waitpid`
- `posix_bind_stdio_to_console` теперь делегирует в `unix_bind_stdio_to_console`.
- Валидация user-range и copy-in строк вынесена в `unix_user_range_ok` и
  `unix_copy_user_cstr`, POSIX слой использует делегирование.

## Последствия

Плюсы:

- Стабильнее ABI-контракт POSIX при эволюции внутренних механизмов.
- Проще заимствовать/сопоставлять Unix-семантику с FreeBSD.
- Чётче границы ответственности модулей.

Минусы:

- Временное дублирование/прокладка API до полного переноса остальных syscall.
- Потребуется поэтапный рефакторинг без «big bang».

## План дальнейшего переноса

P0:

1. Перенести `exec/open/read/write/readdir` Unix-семантику в `kernel/unix`.
2. Ввести единый `copyin/copyout` API в Unix-слое.
3. Закрыть lifecycle регрессии (`run /bin/true`) с wait-channel моделью.

P1:

1. Перенести управление FD (`dup/close-on-exec/fcntl`) в Unix-слой.
2. Перенести path-resolution правила (`AT_FDCWD`, relative/absolute semantics).
3. Подготовить сигнальную модель (`SIGCHLD`, wait semantics) поверх Unix-слоя.
