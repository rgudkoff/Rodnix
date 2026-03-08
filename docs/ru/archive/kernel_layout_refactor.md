# Пересмотр структуры файлов ядра (XNU-inspired)

Этот документ фиксирует целевую структуру каталогов RodNIX для дальнейшей
разработки. Основа идеи взята из XNU: группировка кода по доменам ядра
(`bsd/kern` как доменный слой), а не как "общая папка для всего".

Референс:
- https://github.com/apple-oss-distributions/xnu/tree/main/bsd/kern
- `/Users/romangudkov/dev/Rodnix/docs/ru/xnu_crosswalk.md`

## Что видно в XNU-подходе

- Доменная группировка: процессы, сигналы, ресурсы, sysctl, time и т.д.
  лежат в одном логическом слое `bsd/kern`.
- Имена файлов отражают подсистему (`kern_*`, `uipc_*`, `tty_*`, ...),
  что упрощает навигацию.
- VM, Mach и драйверные части вынесены в другие домены, а не смешаны с BSD-kern.

## Проблема текущей структуры RodNIX

Сейчас значимая часть ядра находится в `kernel/common`, что:
- скрывает границы подсистем;
- усложняет ownership и code review;
- смешивает process/syscall/IPC/shell/debug/memory utility в одном месте.

## Целевая структура RodNIX

```text
kernel/
  arch/
  core/
  kern/            # process/task/scheduler/syscall/ipc/security/bootstrap
  vm/              # heap, page alloc policy, VM helpers (арх-независимые)
  fs/
  net/
  device/          # hardware-facing abstractions (если отделим от fabric)
  fabric/
  input/
  posix/
```

Минимальный маппинг из текущего дерева:
- `kernel/common/task.*` -> `kernel/kern/task.*`
- `kernel/common/scheduler*` -> `kernel/kern/scheduler*`
- `kernel/common/syscall.*` -> `kernel/kern/syscall.*`
- `kernel/common/ipc.*` и `idl_*` -> `kernel/kern/ipc.*`, `kernel/kern/idl_*`
- `kernel/common/security.*` -> `kernel/kern/security.*`
- `kernel/common/bootstrap.*`, `loader.*` -> `kernel/kern/`
- `kernel/common/heap.*` -> `kernel/vm/heap.*`
- `kernel/common/console.*`, `debug.*`, `string.*`, `shell.*`:
  оставить временно в `kernel/common` до отдельного решения по `libkern`/`diag`.

## Правила после миграции

- Новые process/syscall/ipc/scheduler файлы не добавлять в `kernel/common`.
- `common` использовать только как временную зону legacy-кода.
- В include-путях использовать доменные префиксы (`kern/...`, `vm/...`, ...).
- Для каждого домена ввести owner-файл (`OWNERS.md` или секцию в документации).

## План миграции

1. P1 (без изменения поведения):
   - создать `kernel/kern` и `kernel/vm`;
   - перенести только файлы и обновить include/Makefile пути;
   - добиться сборки без функциональных изменений.
2. P2 (закрепление границ):
   - запретить новые файлы в `kernel/common` для доменов `kern/vm`;
   - обновить документацию и CI-проверки на структуру путей.
3. P3 (дальнейшая декомпозиция):
   - выделить `libkern` (string/base utils) и `diag` (shell/debug/console),
     если это даст более чистые зависимости.

## Критерии готовности P1

- Все пути в `kernel/Makefile` обновлены на `kern/` и `vm/`.
- Нет include-путей, ссылающихся на старые перенесенные файлы.
- Сборка проходит, boot smoke-test не меняется.
- В `kernel/common` не осталось process/syscall/ipc/scheduler/task/heap.
