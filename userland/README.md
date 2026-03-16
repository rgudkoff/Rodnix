# Userland (staging)

Этот каталог содержит заготовки пользовательских компонентов RodNIX.
Минимальный запуск userland уже доступен по умолчанию при boot:
ядро загружает `/bin/init` (ELF64), переключает поток в ring3 и использует
базовые POSIX-вызовы (`read/write/exit` и др.).
В userland по умолчанию используется fast entry `syscall/sysret`;
legacy entry `int 0x80` сохранён как fallback.

Текущая схема запуска:
- `/bin/init` — launcher (smoke + `exec("/bin/sh")`).
- `/bin/sh` — интерактивный userspace shell (`sh>`), команды:
  `help`, `pid`, `hostname`, `cd [path]`, `motd`, `uname`, `cat <path>`,
  `smoke`, `ttytest`, `run <path>`, `exec <path>`, `exit`.
  Также поддержан запуск внешних программ без `run`:
  - `sh> <program> [args...]` (ищет `/bin/<program>`, если не задан абсолютный путь).
  - Аргументы передаются в userspace как `argc/argv` (MVP, без envp).
- `/bin/echo` — внешний userspace-бинарник (не built-in shell-команда), запускается:
  - `sh> echo hello world`
- `/bin/ls`, `/bin/cat`, `/bin/true` — внешние userland-заготовки
  (MVP-утилиты для модели "не builtin").
- `/bin/cpuinfo` — подробный отчёт по CPU topology, частоте и CPUID feature flags.
- `/bin/diskinfo` — диагностика блочных устройств (`diskinfo`, `diskinfo -r <dev> <lba>`).
- `/bin/kmodctl` — управление реестром модулей (`kmodctl ls|load|unload`).
- `/etc` в rootfs:
  - `/etc/motd` — приветствие, печатается `init` при старте;
  - `/etc/hostname` — hostname, читается `init`;
  - `/etc/ttys` — задел под описание терминалов.

Ограничения текущего состояния:
- ABI и набор syscalls пока неполные;
- bootstrap‑сервер в userland и сервисный запуск через IPC ещё в работе.

POSIX syscall номера синхронизируются автоматически из
`kernel/posix/syscalls.master` (через master-таблицу), генератор:
`scripts/mkposixsyscalls.py`.

Минимальный POSIX-совместимый заголовочный слой для userland находится в
`userland/include`:
- `unistd.h`, `fcntl.h`, `errno.h`, `signal.h`, `mman.h`
- `dirent.h`, `termios.h`, `time.h`, `pwd.h`, `grp.h`, `limits.h`
- `sys/types.h`, `sys/fcntl.h`, `sys/wait.h`, `sys/stat.h`, `sys/errno.h`
- `sys/signal.h`, `sys/mman.h`, `sys/dirent.h`, `sys/termios.h`, `sys/time.h`

В `userland/libc` подключен `libc-lite` (минимальный runtime-слой):
- `errno` storage;
- базовые `string`/`ctype`/`stdlib`/`stdio` функции;
- handle-based `dirent` API (`opendir/readdir/closedir`);
- базовый file-path API (`stat/fstat/lseek`);
- POSIX-обертки в `unistd.h` возвращают `-1` и выставляют `errno`
  для отрицательных кодов ядра.

Числовые значения ключевых `errno`/`fcntl`/`wait` констант выравниваются с
vendor BSD baseline (`third_party/bsd/*/sys/sys/*`) и проверяются
автоматически в `make -C userland` через:
- `scripts/check_bsd_abi_headers.py`

Синхронизация ABI-заголовков с эталонным baseline выполняется отдельной командой:
- `make sync-bsd-abi` (из корня репозитория), либо
- `make -C userland sync-bsd-abi`

После синхронизации рекомендуется запускать:
- `make check-abi`

Модель команд (традиционный Unix shell-подход):
- команды, меняющие состояние shell-процесса (например, `cd`) должны быть builtin;
- файловые/системные утилиты (`ls`, `cat`, `echo`, и т.п.) развиваются как
  отдельные внешние программы.

План:
- минимальный loader и переход в ring3;
- bootstrap‑сервер/launcher в userland;
- запуск сервисов через IPC.

Тестовый модульный образ:
- при сборке userland автоматически создаётся `/lib/modules/demo.kmod`
  (минимальный `RDKMOD1`-заголовок для проверки `kmodload`/`kmodunload`).
- также создаётся `/lib/modules/demo.ko` (ELF relocatable object с секцией
  `.rodnix_mod`), используемый для ELF-пути загрузки.
