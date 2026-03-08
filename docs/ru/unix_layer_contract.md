# Контракт Слоя `kernel/unix`

Дата: 2026-03-08

Цель: зафиксировать границу слоя Unix-семантики, чтобы `kernel/unix` не
превратился в «свалку POSIX-кода».

## `kernel/unix` отвечает за

- Unix process semantics:
  `spawn/exec/exit/wait` поведение на уровне Unix-модели процесса.
- File descriptor semantics:
  таблица FD, правила `open/close/read/write`, Unix-ошибки.
  Нормативно: `FD_CLOEXEC` трактуется как флаг descriptor entry процесса;
  `spawn` копирует FD-таблицу; `exec` применяет `CLOEXEC` до входа в новый image.
- Path-based syscall semantics:
  поведение `readdir/open` как Unix API-контракт.
- User-facing copyin/copyout policy для Unix syscall-слоя
  (`user-range` проверки, чтение C-строк из userspace).

## `kernel/unix` не отвечает за

- scheduler internals;
- VFS internals (vnode/inode/device backend);
- loader internals (ELF parsing, paging setup);
- memory manager internals;
- driver internals.

## Структура модулей (минимум)

- `kernel/unix/process/*` — process lifecycle semantics
- `kernel/unix/fd/*` — fd semantics
- `kernel/unix/fs/*` — path/readdir semantics
- `kernel/unix/exec/*` — exec/spawn orchestration
- `kernel/unix/uaccess/*` — user copy/validation helpers

## Правило эволюции

Если новая логика требует знания внутренних структур scheduler/vfs/mm/driver,
она должна жить в соответствующем механизме, а в `kernel/unix` добавляется
только адаптация Unix-семантики поверх публичного контракта механизма.
