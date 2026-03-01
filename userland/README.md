# Userland (staging)

Этот каталог содержит заготовки пользовательских компонентов RodNIX.
Минимальный запуск userland уже доступен: `run /bin/init` из shell
загружает ELF64, переключает поток в ring3 и использует `int 0x80`
для базовых POSIX-вызовов (`read/write/exit` и др.).

Ограничения текущего состояния:
- это минимальный путь без полноценной process model (`fork/exec/wait`);
- ABI и набор syscalls пока неполные;
- bootstrap‑сервер в userland и сервисный запуск через IPC ещё в работе.

План:
- минимальный loader и переход в ring3;
- bootstrap‑сервер в userland;
- запуск сервисов через IPC.
