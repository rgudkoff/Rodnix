# Kernel Crosswalk для RodNIX

Документ фиксирует соответствие между текущими подсистемами RodNIX и
целевой доменной структурой для практического проектирования файлов и границ модулей.

## Что изучено в эталонной архитектуре (локально)

Ключевые домены верхнего уровня:
- `bsd/` (POSIX/BSD слой, процессы, syscalls, сокеты, VFS)
- `osfmk/` (Mach/VM, низкоуровневое ядро)
- `iokit/` (драйверный стек)
- `libkern/` (kernel utility/runtime)
- `security/` (политики и security frameworks)

Фокус по задаче:
- `bsd/kern`: доменный слой process/syscalls/resource/sockets/tty/memorystatus.
- `osfmk/vm`: VM map/object/page/fault/pageout/compressor.

## Паттерн именования в `bsd/kern`

По структуре файлов видно стабильный доменный паттерн:
- `kern_*` для process/resource/sysctl/memorystatus.
- `sys_*` для системных интерфейсов.
- `uipc_*` для socket/IPC сетевого уровня.
- `tty_*` для TTY.
- `subr_*` для support routines.

Вывод для RodNIX: критично группировать код по домену ответственности, а не в
папку `common`.

## Crosswalk: RodNIX -> target architecture

| Подсистема RodNIX | Текущее место | target reference | Целевое место RodNIX |
| --- | --- | --- | --- |
| Task/process lifecycle | `kernel/common/task.c`, `kernel/core/task.h` | `bsd/kern/kern_proc.c`, `kern_fork.c`, `kern_exit.c` | `kernel/kern/task.c`, `kernel/kern/task.h` |
| Scheduler policy/runtime | `kernel/common/scheduler/*` | `bsd/kern/kern_synch.c`, `kern_sched*` (распределено) | `kernel/kern/scheduler/*` |
| Syscall dispatch | `kernel/common/syscall.c`, `kernel/posix/posix_syscall.c` | `bsd/kern/sys_*.c`, `syscalls.master` | `kernel/kern/syscall.c` + `kernel/posix/*` |
| Security credentials/policy | `kernel/common/security.c` | `bsd/kern/kern_credential.c`, `policy_check.c` | `kernel/kern/security.c` |
| IPC (базовый) | `kernel/common/ipc.c`, `idl_runtime.c` | `bsd/kern/uipc_*.c`, `osfmk/ipc/*` | `kernel/kern/ipc.c`, `kernel/kern/idl_*` |
| Memory pressure / OOM policy | docs-only (`docs/ru/memorystatus.md`) | `bsd/kern/kern_memorystatus*.c` | `kernel/kern/memorystatus/*` (новое) |
| VM policy/helpers | `kernel/common/heap.c`, `arch/x86_64/pmm.c/paging.c` | `osfmk/vm/vm_*.c`, `pmap.*` | `kernel/vm/*` + `kernel/arch/*/pmap|paging` |
| FS/VFS | `kernel/fs/vfs.c` | `bsd/vfs/*`, частично `bsd/kern/ubc_subr.c` | `kernel/fs/*` (оставить домен) |
| Net/socket | `kernel/net/net.c`, `kernel/net/socket.c` | `bsd/net*`, `bsd/kern/uipc_*.c`, `sys_socket.c` | `kernel/net/*` (с явным разделением на stack/socket) |
| Device fabric | `kernel/fabric/*`, `kernel/input/*` | `iokit/*` + `bsd/dev/*` | `kernel/fabric/*`, `kernel/device/*` (опционально) |
| Console/debug/shell | `kernel/common/console.c`, `debug.c`, `shell.c` | `bsd/kern/subr_*.c`, `osfmk/console/*` | `kernel/diag/*` (позже), временно `kernel/common/*` |
| String/util | `kernel/common/string.c` | `libkern/*`, `bsd/kern/subr_*.c` | `kernel/libkern/*` (позже), временно `kernel/common/*` |

## Что берем из эталонной архитектуры, а что нет

Берем:
- доменное разбиение файлов;
- явные префиксы и ownership подсистем;
- разделение `policy` и `mechanics` в VM/memorystatus.

Не берем напрямую:
- точные ABI/API внешних систем;
- Mach-specific сложность, не нужную RodNIX на текущем этапе;
- избыточные слои, пока нет требований масштаба.

## Рекомендуемая целевая структура (уточненная)

```text
kernel/
  arch/
  core/
  kern/          # process/syscall/ipc/security/scheduler/memorystatus
  vm/            # vm_map/vm_object/fault/pageout/heap policy
  fs/
  net/
  fabric/
  input/
  posix/
  common/        # legacy (временная зона)
```

## Следующие шаги (практические)

1. Перенести P1-критичные файлы из `kernel/common` в `kernel/kern` и `kernel/vm`
   без изменения логики.
2. Добавить `kernel/kern/memorystatus/` с каркасом API из
   `docs/ru/memorystatus.md`.
3. Ввести правило ревью: новые process/vm файлы в `common` не принимаются.
4. После стабилизации выделить `kernel/diag` и `kernel/libkern`.
