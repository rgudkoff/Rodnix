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
- Есть встроенный VFS-узел `/dev/console` (special inode), через который идут
  консольные `read/write` для userland.

## Инварианты

- Все ФС подключаются через VFS интерфейс.
- Name cache должен инвалидироваться при изменениях в дереве.
- Initrd/ramfs должны быть доступны до запуска userland.

## Где смотреть в коде

- `kernel/fs/vfs.c`, `kernel/fs/vfs.h`, `kernel/fs/initrd.h`.
