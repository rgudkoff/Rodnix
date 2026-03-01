# BSD POSIX/Userland Plan

Этот документ фиксирует, как мы переносим POSIX и userland-подход в Rodnix
на базе BSD-кода, поэтапно и без регрессий boot/runtime.

## Что уже импортировано

- Vendor baseline в `third_party/bsd/freebsd-src/`:
  - `bin/sh/*` (исходники shell как референс-база)
  - выбранные POSIX заголовки из `include/`
  - `sys/sys/{cdefs.h,queue.h,tree.h}` (локальный снимок)

Важно: этот baseline пока не подключен в сборку Rodnix напрямую.

## Почему не подключаем сразу

- Полный `bin/sh` и libc в BSD опираются на широкий слой ABI/CRT/libc.
- Текущий Rodnix userland минималистичен и использует ограниченный набор syscalls.
- Прямое включение без адаптера даст множество несовместимостей.

## Этапы интеграции

1. POSIX ABI alignment:
- выровнять сигнатуры и коды ошибок в syscall ABI;
- расширить ядро до минимального набора `open/read/write/close/fstat/lseek/mmap/brk/sigaction`.

2. Userland libc shim:
- ввести промежуточный `libc-lite` с BSD-совместимыми заголовками;
- подключить `errno`, `fcntl`, `sys/types`, `sys/stat`, `sys/wait` и базовые string/stdio wrappers.

3. Shell migration:
- сначала перенести лексер/парсер как отдельный таргет;
- затем подключить builtins и execution path;
- только после этого сделать BSD-shell `sh` default в rootfs.

## Что нужно дополнительно скачать из FreeBSD

Для следующего шага (POSIX ABI + libc-lite) нужны как минимум:

- `sys/sys/errno.h`
- `sys/sys/fcntl.h`
- `sys/sys/types.h`
- `sys/sys/stat.h`
- `sys/sys/wait.h`
- `sys/sys/mman.h`
- `sys/sys/unistd.h`

(и связанные include-зависимости по месту).

## Критерии готовности этапа

- `make` + `smoke_qemu` проходят без регрессий.
- userland `init`/`sh` запускаются стабильно.
- в документации обновлены ABI и список поддерживаемых POSIX API.
