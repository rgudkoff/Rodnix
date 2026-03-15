# VFS

## Цели

- Модель vnode/inode.
- Кэш имён (name cache).
- Монтирование файловых систем.
- Initrd/ramfs для старта системы.

## MVP

- Базовые структуры vnode/inode.
- Name cache для ускорения lookup (простая инвалидация по генерации).
- Поддержка монтирования корневой ФС.
- Ramfs как стартовая ФС, возможность подхвата initrd.

## Текущая реализация (v0)

- Реализован VFS-скелет с `vfs_node_t` (vnode) и `vfs_inode_t` (inode).
- Есть таблица mount (root ramfs + возможность подключать ramfs на путь).
- Name cache: фиксированная таблица, инвалидация на любом изменении дерева.
- Initrd: поддержан простой формат `RDNX` (таблица файлов), импортируется в RAMFS.
- Есть `vfs_mount_initrd_root()` для замены корня на initrd‑RAMFS.
- Весь доступ сейчас идёт через RAMFS (in-memory).
 - Initrd подключается из boot‑модуля (Multiboot2 module) и импортируется в RAMFS.
- Зарегистрирован драйвер `ext2`:
  - чтение superblock/group descriptors;
  - чтение inode/directories и построение дерева VFS при mount;
  - write-path реализован для regular files (`write`, `truncate`, `ftruncate`);
  - поддержаны direct + single + double indirect blocks (файлы до ~4 ГБ);
  - preload-лимит при mount: 64 МБ (файлы большего размера обрезаются при загрузке в VFS-кэш);
  - освобождение блоков при shrink и обновление счетчиков group/superblock.
- Узлы `/dev` сейчас создаются ядром виртуально (не читаются с диска):
  `/dev/console`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`.
- Консольные узлы `/dev/*` обслуживаются через `kernel/common/tty_console.c`
  (минимальный line discipline: echo, backspace, `Ctrl-U`, `Ctrl-C`, `Ctrl-D`,
  canonical line mode).

## `/dev` и `devfs`

`devfs` реализован как отдельная файловая система (`kernel/fs/devfs.c`),
монтируется в `/dev` на этапе boot.

Текущее состояние:

- `devfs` монтируется в `/dev` через `vfs_mount("devfs", NULL, "/dev")`.
- Символы: `console`, `stdin`, `stdout`, `stderr` (chardev, CONSOLE flag),
  `null` (DEV_NULL), `zero` (DEV_ZERO).
- Блочные устройства регистрируются через `devfs_register_blockdev(name)` —
  вызывается Fabric при attach `disk0` и т.п.
- Динамическая регистрация до mount: устройства ставятся в pending-очередь
  и добавляются при монтировании devfs.

Ещё не реализовано:

- `/dev/fd/{0,1,2}` как ссылки на открытые fd.
- Динамический detach (удаление device node при отсоединении устройства).

## Инварианты

- Все ФС подключаются через VFS интерфейс.
- Name cache должен инвалидироваться при изменениях в дереве.
- Initrd/ramfs должны быть доступны до запуска userland.

## Где смотреть в коде

- `kernel/fs/vfs.c`, `kernel/fs/vfs.h`, `kernel/fs/initrd.h`.
