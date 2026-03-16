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
    `motd`, `uname`, `smoke`, `ttytest`, `run <path>`, `exec <path>`, `exit`).
  - shell умеет запускать внешние программы напрямую:
    `<program> [args...]` (по умолчанию как `/bin/<program>`, либо абсолютный путь).
  - shell поддерживает базовый Unix-синтаксис конвейеров и редиректов:
    - `cmd1 | cmd2`
    - `cmd > file`, `cmd >> file`
    - `cmd < file`
    - `cmd 2> file`, `cmd 2>> file`
    - при `exit != 0` shell не считает команду "не найденной";
      сообщение `command not found or failed` используется для реальной ошибки spawn/exec.
  - принята традиционная Unix-модель команд shell:
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
- `ttytest` используется для ручной проверки line discipline
  (backspace, canonical newline).
- Добавлен минимальный TTY control-plane через `ioctl`:
  - `isatty(fd)` через `RDNX_TTY_IOCTL_ISATTY`;
  - `tcgetattr/tcsetattr(TCSANOW)` через `RDNX_TTY_IOCTL_GETATTR/SETATTR`
    (termios-lite: `ECHO`, `ECHOCTL`, `ISIG`, `ICANON`, `IEXTEN` + `c_cc[]`).
  - Поддержано базовое control-char поведение через `c_cc`:
    `VINTR`, `VERASE`, `VKILL`, `VEOF`.
- В POSIX ABI добавлены `rename` и `nanosleep`.
- В POSIX ABI добавлены базовые сигналы:
  - `kill`
  - `sigaction`
  - `sigreturn`
  (MVP: базовый handler/restorer путь, без process-groups и расширенной mask-логики).
- Добавлена userspace-утилита `/bin/sleep` (`sleep <seconds>`).
- Добавлена userspace-утилита `/bin/sigtest` для проверки сигналов.
- Добавлена userspace-утилита `/bin/stty` для управления termios-lite:
  - профили `raw` (безопасный raw-lite), `-raw`/`cooked`, `sane`;
  - флаги `echo/-echo`, `isig/-isig`, `icanon/-icanon`, `iexten/-iexten`, `echoctl/-echoctl`;
  - control chars `intr`, `erase`, `kill`, `eof` (например `stty intr ^C`).
- Добавлена утилита `/bin/ifconfig` (userspace), работающая через syscall
  `netiflist` и Fabric net-service.
- Добавлена userspace-утилита `/bin/diskinfo`:
  - `diskinfo` — список блочных устройств;
  - `diskinfo -r <dev> <lba>` — чтение сектора через `blockread`.
- Добавлена userspace-утилита `/bin/kmodctl`:
  - `kmodctl ls` — список модулей;
  - `kmodctl load <path>` / `kmodctl unload <name>`.
- Для CI есть авто-сценарий `/etc/smoke.ifconfig.auto`:
  `init` запускает `/bin/ifconfig`, ждёт завершения и печатает `[SMK]` маркеры.
- Таблица POSIX syscall-ов теперь ведётся через master-таблицу:
  - master-файл: `kernel/posix/syscalls.master`;
  - генерация: `scripts/mkposixsyscalls.py`;
  - output: `kernel/posix/posix_sysnums.h`, `userland/include/posix_sysnums.h`,
    `kernel/posix/posix_sysent.inc`.
- Для kmod-пути в rootfs собираются тестовые образы:
  - `/lib/modules/demo.kmod` (header-only формат `RDKMOD1`);
  - `/lib/modules/demo.ko` (ELF relocatable с секцией `.rodnix_mod`).

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
  - `rename` (переименование/перемещение в VFS)
  - `ioctl` (минимум для console TTY)
  - `nanosleep` (таймауты в userspace)
  - `kill/sigaction/sigreturn` (базовый signal path)
  - `blocklist/blockread` (дисковая диагностика из userland)
  - `kmodls/kmodload/kmodunload` (контур реестра модулей)

## Инварианты

- Все пользовательские сервисы получают порты через bootstrap.
- Прямые "захардкоженные" порты запрещены.

## Где смотреть в коде

- `userland/init/`, `userland/shell/`, `userland/include/`.
- `kernel/common/loader.c` и `kernel/arch/x86_64/usermode.c`.
- `kernel/common/shell.c` (команда `run`, lifecycle shell/user thread).
- `scripts/mkinitrd.py`, `boot/grub/grub.cfg`.
