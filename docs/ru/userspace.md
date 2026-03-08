# Userland и bootstrap

## Цели

- Минимальный userland для старта сервисов.
- Bootstrap‑порт и bootstrap‑сервер, раздающий порты сервисов
  (аналог launchd в Darwin).

## Bootstrap‑сервер (MVP)

- Запускается одним из первых userland процессов.
- Принимает запросы на регистрацию сервисов.
- Выдает порты сервисов по имени/типу.

## Статус (сейчас)

- Есть минимальный ELF‑loader и запуск ring3 через `loader_exec()`.
- Syscall boundary (0x80) активен, есть базовые POSIX‑syscalls.
- В ядре зарезервирован bootstrap‑порт (placeholder), протокола нет.
- Есть временный kernel‑mode bootstrap server (thread), отвечающий статусом `0`.
- Есть загрузка ELF64 ET_EXEC/PT_LOAD в user PML4 (ядро в higher‑half).
- Добавлена базовая ring3‑инфраструктура (GDT user‑сегменты + TSS RSP0).
- `shell run` поднимает отдельный user task и будит shell после `posix_exit`.
- Initrd поддерживается как источник файлов (`/bin/init`, `/bin/sh`).
- По умолчанию bootstrap идёт через userspace `init` (не через kernel shell).
- Userspace разделён на 2 программы:
  - `/bin/init` — launcher: smoke + `exec("/bin/sh")`;
  - `/bin/sh` — интерактивный shell (`help`, `pid`, `hostname`, `cd [path]`,
    `motd`, `uname`, `cat`, `smoke`, `ttytest`, `run <path>`, `exec <path>`, `exit`).
  - shell умеет запускать внешние программы напрямую:
    `<program> [args...]` (по умолчанию как `/bin/<program>`, либо абсолютный путь).
  - принята модель FreeBSD по командам shell:
    - stateful-команды (минимум `cd`) остаются builtin в `sh`;
    - утилиты (`ls`, `cat`, `echo`, и др.) развиваются как отдельные
      userland-бинарники в `/bin`/`/usr/bin`.
  - В rootfs добавлены внешние заготовки утилит `/bin/ls`, `/bin/cat`, `/bin/true`.
- В rootfs введён базовый `/etc`:
  - `/etc/motd` печатается `init` при старте;
  - `/etc/hostname` читается `init` и логируется;
  - `/etc/ttys` — минимальный конфиг-конвенция для console tty.
- stdin/stdout/stderr идут через POSIX `read/write` (fd `0/1/2`);
  в VFS созданы узлы `/dev/console`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`.
  На текущем этапе это виртуальные VFS-узлы (до выделения отдельного `devfs`).
- `ttytest` используется для ручной проверки line discipline (`Ctrl-U`,
  `Ctrl-D`, backspace, canonical newline).
- Добавлена утилита `/bin/ifconfig` (userspace), работающая через syscall
  `netiflist` и Fabric net-service.
- Для CI есть авто-сценарий `/etc/smoke.ifconfig.auto`:
  `init` запускает `/bin/ifconfig`, ждёт завершения и печатает `[SMK]` маркеры.
- Таблица POSIX syscall-ов теперь ведётся в стиле XNU:
  - master-файл: `kernel/posix/syscalls.master`;
  - генерация: `scripts/mkposixsyscalls.py`;
  - output: `kernel/posix/posix_sysnums.h`, `userland/include/posix_sysnums.h`,
    `kernel/posix/posix_sysent.inc`.

## Проверенный smoke‑test

- По умолчанию: автозапуск `/bin/init` при boot.
- Альтернатива: `run /bin/init` из kernel shell (debug mode).
- Проверенные syscall-и в userland:
  - `getpid`
  - `open/read/close` (чтение ELF заголовка `/bin/init`)
  - `write` (лог в консоль)
  - `exec` (`/bin/init -> /bin/sh`, а также shell-команда `exec <path>`)
  - `spawn/waitpid` (shell-команда `run <path>`)
  - передача `argc/argv` в userspace для запуска внешних программ из shell
  - `exit` (возврат управления в shell)

## Инварианты

- Все пользовательские сервисы получают порты через bootstrap.
- Прямые "захардкоженные" порты запрещены.

## Где смотреть в коде

- `userland/init/`, `userland/shell/`, `userland/include/`.
- `kernel/common/loader.c` и `kernel/arch/x86_64/usermode.c`.
- `kernel/common/shell.c` (команда `run`, lifecycle shell/user thread).
- `scripts/mkinitrd.py`, `boot/grub/grub.cfg`.
