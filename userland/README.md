# Userland (staging)

Этот каталог содержит заготовки пользовательских компонентов RodNIX.
Минимальный запуск userland уже доступен по умолчанию при boot:
ядро загружает `/bin/init` (ELF64), переключает поток в ring3 и использует
`int 0x80` для базовых POSIX-вызовов (`read/write/exit` и др.).

Текущая схема запуска:
- `/bin/init` — launcher (smoke + `exec("/bin/sh")`).
- `/bin/sh` — интерактивный userspace shell (`sh>`), команды:
  `help`, `pid`, `hostname`, `motd`, `uname`, `cat <path>`,
  `smoke`, `ttytest`, `exec <path>`, `exit`.
- `/etc` в rootfs:
  - `/etc/motd` — приветствие, печатается `init` при старте;
  - `/etc/hostname` — hostname, читается `init`;
  - `/etc/ttys` — задел под описание терминалов.

Ограничения текущего состояния:
- это минимальный путь без полноценной process model (`fork/wait`);
- ABI и набор syscalls пока неполные;
- bootstrap‑сервер в userland и сервисный запуск через IPC ещё в работе.

План:
- минимальный loader и переход в ring3;
- bootstrap‑сервер/launcher в userland;
- запуск сервисов через IPC.
