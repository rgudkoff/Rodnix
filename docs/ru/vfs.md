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
- Зарегистрирован драйвер `ext2` (read-only mount path):
  - чтение superblock/group descriptors;
  - чтение inode/directories и построение дерева VFS при mount;
  - write-path пока не реализован.
- Узлы `/dev` сейчас создаются ядром виртуально (не читаются с диска):
  `/dev/console`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`.
- Консольные узлы `/dev/*` обслуживаются через `kernel/common/tty_console.c`
  (минимальный line discipline: echo, backspace, `Ctrl-U`, `Ctrl-C`, `Ctrl-D`,
  canonical line mode).

## `/dev` и `devfs` (план)

Текущее состояние уже виртуализирует `/dev`, но это пока часть общего VFS/RAMFS
кода, а не отдельная файловая система устройств.

Целевое состояние:

- Ввести отдельный тип ФС `devfs`.
- Монтировать `devfs` в `/dev` на раннем этапе boot.
- Перенести создание `console/stdin/stdout/stderr` из общего VFS в `devfs`.
- Добавить `devfs`-пространство `/dev/fd/{0,1,2}`.
- Оформить `stdin/stdout/stderr` как alias-ссылки на `/dev/fd/0,1,2`.
- Добавить единый API динамической публикации устройств
  (например, `devfs_register_chrdev(...)`).

Практический эффект:

- `/dev` становится явно виртуальным и независимым от initrd/rootfs контента.
- Упрощается регистрация драйверов и создание device nodes при attach/detach.
- Архитектура ближе к подходу с управлением device namespace внутри ядра.

## Инварианты

- Все ФС подключаются через VFS интерфейс.
- Name cache должен инвалидироваться при изменениях в дереве.
- Initrd/ramfs должны быть доступны до запуска userland.

## Где смотреть в коде

- `kernel/fs/vfs.c`, `kernel/fs/vfs.h`, `kernel/fs/initrd.h`.
