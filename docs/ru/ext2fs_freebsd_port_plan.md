# План порта FreeBSD ext2fs (RW) в Rodnix

## Цель

Реализовать запись на EXT2-диск в Rodnix через перенос алгоритмов из
`freebsd-src/sys/fs/ext2fs/*` без переноса целиком FreeBSD kernel VFS.

## Исходная база

- Текущий драйвер Rodnix: `kernel/fs/ext2.c` (mount/read + ограниченный writeback).
- FreeBSD источники:
  - `/Users/romangudkov/dev/freebsd-src/sys/fs/ext2fs/ext2_balloc.c`
  - `/Users/romangudkov/dev/freebsd-src/sys/fs/ext2fs/ext2_alloc.c`
  - `/Users/romangudkov/dev/freebsd-src/sys/fs/ext2fs/ext2_inode.c`
  - `/Users/romangudkov/dev/freebsd-src/sys/fs/ext2fs/ext2_subr.c`
  - `/Users/romangudkov/dev/freebsd-src/sys/fs/ext2fs/ext2_dinode.h`
  - `/Users/romangudkov/dev/freebsd-src/sys/fs/ext2fs/ext2fs.h`
  - `/Users/romangudkov/dev/freebsd-src/sys/fs/ext2fs/fs.h`

## Почему не прямой перенос `ext2_vfsops.c/ext2_vnops.c`

Эти файлы опираются на FreeBSD-слои:
- `struct vnode`, `struct mount`, `namei`.
- Buffer cache (`struct buf`, `bread/bwrite`, буферные флаги).
- Локи, креды, квоты, extattr, VM хуки.

В Rodnix этого API нет, поэтому нужен адаптированный перенос логики
аллокации/маппинга блоков и inode-обновления, а не перенос VFS glue-кода.

## Архитектура порта в Rodnix

Добавить внутренний слой `ext2_rw_core` под `kernel/fs/ext2.c`:

- `ext2_rw_alloc_block(...)`
- `ext2_rw_free_block(...)`
- `ext2_rw_map_or_alloc_lbn(...)`
- `ext2_rw_update_inode_size(...)`
- `ext2_rw_commit_super_group_counters(...)`

`ext2.c` остается точкой интеграции с Rodnix VFS (`vfs_write`, `vfs_truncate`).

## Этапы

### Этап 0: Подготовка (текущая ветка `codex/fs`)

- [x] Создать ветку для порта.
- [ ] Подтянуть в Rodnix нужные ext2fs-заголовки FreeBSD в отдельный namespace
      (только структуры/константы, без vnodes).
- [ ] Добавить тестовый ext2-образ для сценариев роста файла и truncate.

### Этап 1: Блочная аллокация

- [x] Реализовать чтение/обновление block bitmap.
- [x] Реализовать выбор группы и поиск свободного блока.
- [x] Обновлять `free_blocks_count` в group descriptor и superblock.

### Этап 2: Рост файла и запись

- [x] Расширить `ext2_writeback_file`:
  - аллокация новых data blocks;
  - поддержка direct + single indirect (минимальный рабочий набор);
  - запись новых block pointers в inode/indirect block.
- [x] Обновлять размер inode на диске.

### Этап 3: truncate/ftruncate

- [x] Поддержать увеличение и уменьшение файла.
- [x] При уменьшении освобождать блоки и корректировать счетчики.

### Этап 4: Надежность

- [ ] Гарантировать порядок записи метаданных (минимум consistency без журнала).
- [ ] Добавить проверки ошибок I/O и rollback там, где возможно.

## Минимальный MVP RW

- Регулярные файлы.
- Блоки: direct + single indirect.
- `write`, `truncate`, `ftruncate` на уже смонтированном ext2.
- Без extents, xattr, ACL, htree.

## Риски

- Неконсистентность при сбое питания (у ext2 нет журнала).
- Коррупция при неверном порядке обновления bitmap/inode/counters.
- Границы совместимости по feature flags ext2/ext3/ext4.

## Критерии готовности

- `cat > /mnt/file` с ростом файла работает стабильно.
- Повторный mount читает записанные данные корректно.
- `truncate` освобождает блоки (счетчики в superblock/group меняются ожидаемо).
- Базовые контрактные тесты Rodnix проходят без регресса.
