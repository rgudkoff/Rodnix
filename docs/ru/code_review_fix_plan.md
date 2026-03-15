# RodNIX — План устранения замечаний code review

Дата: 2026-03-15
Ветка: `fs`
Статус: к выполнению

---

## Контекст

Документ фиксирует результаты полного обхода кодовой базы и план устранения
выявленных проблем. Замечания разделены на приоритеты P0–P2 и сгруппированы
в четыре исполнительных фазы плюс подготовительную фазу 0.

---

## Выявленные проблемы

### P0 — Критические (блокеры корректности)

#### P0-1: EXT2 writeback без блокировки (RACE CONDITION)

**Файл:** `kernel/fs/ext2.c`, функции `ext2_writeback_file` (~строки 900–989),
`ext2_resize_file` (~строки 1000–1050).

`ext2_writeback_file` читает инод, аллоцирует блоки и пишет данные без удержания
какого-либо лока. `ext2_alloc_block` и `ext2_free_block` работают с одним глобальным
контекстом `g_ext2_live`. Два потока, одновременно записывающих в разные файлы,
гарантированно коррумпируют bitmap блоков или суперблок.

#### P0-2: Refcount IPC-портов не атомарен с постановкой в очередь (RACE CONDITION)

**Файл:** `kernel/common/ipc.c`, `ipc_send` (~строки 350–367).

`ref_count++` для встроенных прав портов и `ipc_queue_push` не защищены единым
локом. Между инкрементом и постановкой в очередь другой поток может вызвать
`port_deallocate`, обнулить refcount и освободить порт. Rollback-путь затем пишет
в уже освобождённую память.

#### P0-3: `current_task` / `current_thread` — глобалы, не per-CPU (SMP UNSAFE)

**Файл:** `kernel/common/task.c` ~строки 21–22,
`kernel/common/scheduler/internal.h` ~строка 26.

```c
static task_t* current_task = NULL;
static thread_t* current_thread = NULL;
```

На SMP все CPU используют одни и те же указатели. CPU 0 и CPU 1 перезаписывают
друг другу `current_task` при переключении контекста.

#### P0-4: `task_destroy` не уничтожает потоки задачи (RESOURCE LEAK)

**Файл:** `kernel/common/task.c` ~строки 263–288.

`task_destroy` закрывает fd_table, вызывает `vm_task_destroy`, затем `kfree(task)`.
Потоки задачи и их kernel-стеки не освобождаются. `task_t` хранит `main_thread`,
но нет обхода всех потоков.

---

### P1 — Высокий приоритет (стабильность)

#### P1-5a: Отсутствие rollback при частичной аллокации стека в loader

**Файл:** `kernel/common/loader.c` ~строки 160–183.

`loader_map_stack` при OOM на итерации `i` возвращает ошибку, не освобождая
страницы, смапленные на итерациях `0..i-1`. Аналогично `loader_load_elf` не
размапливает уже загруженные сегменты при ошибке.

#### P1-5b: Rollback `ext2_resize_file` при grow глотает ошибку trim

**Файл:** `kernel/fs/ext2.c` ~строки 1036–1038.

```c
(void)ext2_trim_inode_blocks(...);  // ошибка trim игнорируется
```

При сбое trim диск остаётся в несогласованном состоянии, и это нигде не логируется.

#### P1-6: VFS cache возвращает потенциально висячие указатели (UAF)

**Файл:** `kernel/fs/vfs.c` ~строки 62–76.

`vfs_cache_lookup` возвращает `node` напрямую. `vfs_unlink` вызывает
`vfs_free_node(node)`, а затем `vfs_cache_reset()`, инкрементируя `vfs_cache_gen`.
Поток, получивший указатель из кеша до сброса генерации, держит dangling pointer.
Generation counter предотвращает будущие cache hit, но не инвалидирует уже
выданные указатели.

#### P1-7: Отсутствует документация о том, какой лок что защищает

**Файлы:** `ext2.c`, `vfs.c`, `ipc.c`, `task.c`, `heap.c`.

`g_ext2_live`, `vfs_mounts`, `vfs_cache`, `all_tasks_by_id`, таблица портов —
нигде не указано, каким локом защищён каждый глобал.

---

### P2 — Средний приоритет (корректность edge cases)

#### P2-8: `ext2_inode_size_bytes` игнорирует `size_high` (silent truncation >4GB)

**Файл:** `kernel/fs/ext2.c` ~строки 128–132.

```c
uint64_t sz = ino ? (uint64_t)ino->size_lo : 0;  // size_high не читается
```

Файлы >4 GB молча усекаются. Поле `size_high` инода не используется ни при чтении,
ни при записи.

#### P2-9: Стек priority inheritance фиксирован глубиной 4, переполнение молчит

**Файлы:** `kernel/core/task.h` ~строка 155,
`kernel/common/scheduler/control.c` ~строки 201–207.

```c
int16_t inherit_stack[4];
// ...
if (target->inherit_depth < 4) { /* при 5+ — push молча пропускается */ }
```

При цепочке из 5+ потоков с инверсией приоритетов inheritance теряется без ошибки.
Pop при этом восстанавливает некорректное значение.

#### P2-10: VFS write коллапсирует все ошибки ext2 в `RDNX_E_UNSUPPORTED`

**Файл:** `kernel/fs/vfs.c` ~строки 885–889.

```c
if (wrc != RDNX_E_INVALID && wrc != RDNX_E_NOMEM) {
    return RDNX_E_UNSUPPORTED;  // RDNX_E_NOTFOUND, device errors, etc. — теряются
}
```

Caller не может отличить полный диск от ошибки прав доступа.

---

## План исправлений

### Граф зависимостей

```
0-A (ext2 spinlock field)   ────────────────► P0-1
0-B (port table lock)       ────────────────► P0-2
0-C (per-CPU locals array)  ────────────────► P0-3, P0-4
0-D (thread-list in task_t) ────────────────► P0-4
paging_unmap (verify/add)   ────────────────► P1-5a
P0-1                        ────────────────► P1-5b (под тем же локом)
P1-6 Part A                 ────────────────► P1-6 Part B (Phase 4)
```

---

### Phase 0 — Инфраструктура (дни 1–3)

Подготовительные изменения без изменения поведения. Выполняются первыми.

---

#### 0-A: Spinlock в `ext2_mount_ctx_t`

**Файл:** `kernel/fs/ext2.c`

Добавить поле в структуру (`kernel/fs/ext2.h` или начало `ext2.c`, где объявлен
`ext2_mount_ctx_t`):

```c
#include "../fabric/spin.h"

typedef struct {
    /* ... существующие поля ... */
    spinlock_t lock;
} ext2_mount_ctx_t;
```

В `ext2_mount`, после `memset(&ctx, 0, sizeof(ctx))`:
```c
spinlock_init(&ctx.lock);
```

И перед `g_ext2_live = ctx`:
```c
spinlock_init(&g_ext2_live.lock);
```

`spinlock_t` уже используется в `kernel/fabric/spin.h` — новой зависимости нет.

*Сложность: малая. Риск: нулевой.*

---

#### 0-B: Глобальный лок таблицы IPC-портов

**Файл:** `kernel/common/ipc.c`

```c
static spinlock_t g_port_table_lock;

int ipc_init(void) {
    spinlock_init(&g_port_table_lock);
    /* ... */
}
```

*Сложность: малая. Риск: нулевой.*

---

#### 0-C: Per-CPU массив вместо глобальных `current_task` / `current_thread`

**Файлы:** `kernel/common/task.c`, `kernel/core/task.h`

Создать тип (можно в `kernel/core/task.h` или отдельном `kernel/core/percpu.h`):

```c
#define MAX_CPUS 8

typedef struct {
    task_t*   task;
    thread_t* thread;
} cpu_local_t;
```

В `task.c` заменить:
```c
// Убрать:
static task_t*   current_task   = NULL;
static thread_t* current_thread = NULL;

// Добавить:
static cpu_local_t g_cpu_locals[MAX_CPUS];
```

Переписать аксессоры:
```c
task_t*   task_get_current(void)          { return g_cpu_locals[cpu_id()].task; }
void      task_set_current(task_t* t)     { g_cpu_locals[cpu_id()].task = t; }
thread_t* thread_get_current(void)        { return g_cpu_locals[cpu_id()].thread; }
void      thread_set_current(thread_t* t) { g_cpu_locals[cpu_id()].thread = t; }
```

`cpu_id()` для UP всегда возвращает 0 — поведение идентично текущему.
Для SMP требуется корректный `cpu_id()` через APIC ID или GSBASE.

*Сложность: средняя. Риск: низкий (UP), требует верификации cpu_id на SMP.*

---

#### 0-D: Список потоков внутри `task_t`

**Файлы:** `kernel/core/task.h`, `kernel/common/task.c`

В `task_t`:
```c
TAILQ_HEAD(thread_list_head, thread) threads;
```

В `thread_t`:
```c
TAILQ_ENTRY(thread) task_link;
```

В `task_create`:
```c
TAILQ_INIT(&task->threads);
```

В `thread_create` / `thread_create_user_clone` после создания потока:
```c
TAILQ_INSERT_TAIL(&task->threads, thread, task_link);
```

В `thread_destroy`:
```c
TAILQ_REMOVE(&thread->task->threads, thread, task_link);
```

*Сложность: малая. Риск: низкий — чисто аддитивно.*

---

### Phase 1 — P0: Критические баги (дни 4–7)

---

#### P0-1: Блокировка ext2 writeback и resize

**Файл:** `kernel/fs/ext2.c`

Добавить захват/освобождение `g_ext2_live.lock` в начале и перед каждым `return`
в `ext2_writeback_file` и `ext2_resize_file`. Паттерн:

```c
int ext2_writeback_file(...) {
    /* валидация параметров */
    if (!node || ...) return RDNX_E_INVALID;

    spinlock_lock(&g_ext2_live.lock);

    ext2_inode_t ino;
    int rc = ext2_read_inode(&g_ext2_live, (uint32_t)node->inode->fs_ino, &ino);
    if (rc != RDNX_OK) {
        spinlock_unlock(&g_ext2_live.lock);
        return rc;
    }
    /* ... все существующие операции ... */

    spinlock_unlock(&g_ext2_live.lock);
    return RDNX_OK;
}
```

`ext2_alloc_block`, `ext2_free_block`, `ext2_sync_super_and_gdt` — не захватывают
лок самостоятельно. Добавить комментарий:

```c
/* LOCKING: caller must hold g_ext2_live.lock */
static uint32_t ext2_alloc_block(ext2_ctx_t* ctx) { ... }
```

**Внимание:** убедиться, что `fabric_blockdev_read/write` не вызывает ext2 рекурсивно,
иначе возможен дедлок. Проверить call graph.

*Сложность: средняя (много точек выхода). Риск: средний.*

---

#### P0-2: Атомарность refcount IPC-портов

**Файл:** `kernel/common/ipc.c`

В `ipc_send` — удерживать `g_port_table_lock` через весь цикл:

```c
spinlock_lock(&g_port_table_lock);

uint32_t bumped = 0;
for (uint32_t i = 0; i < message->port_count; i++) {
    port_t* p = port_lookup(message->ports[i]);
    if (!p || !p->active) {
        /* rollback уже bumped */
        for (uint32_t j = 0; j < bumped; j++) {
            port_t* q = port_lookup(message->ports[j]);
            if (q) q->ref_count--;
        }
        spinlock_unlock(&g_port_table_lock);
        return RDNX_E_INVALID;
    }
    p->ref_count++;
    bumped++;
}

int qrc = ipc_queue_push(...);
if (qrc != 0) {
    for (uint32_t j = 0; j < bumped; j++) {
        port_t* q = port_lookup(message->ports[j]);
        if (q) q->ref_count--;
    }
    spinlock_unlock(&g_port_table_lock);
    return RDNX_E_BUSY;
}

spinlock_unlock(&g_port_table_lock);
```

В `port_deallocate`:
```c
spinlock_lock(&g_port_table_lock);
if (port->ref_count == 0) {
    spinlock_unlock(&g_port_table_lock);
    return;  /* underflow guard */
}
port->ref_count--;
bool do_free = (port->ref_count == 0);
if (do_free) { port_table[port->id] = NULL; }
spinlock_unlock(&g_port_table_lock);

if (do_free) {
    ipc_queue_destroy(&port->queue);
    kfree(port);
}
```

**Порядок локов:** `g_port_table_lock` → `q->lock`. Нарушать нельзя.

*Сложность: средняя. Риск: средний.*

---

#### P0-3: Per-CPU аксессоры в scheduler

**Файлы:** `kernel/common/task.c`, `kernel/common/scheduler/internal.h`,
`kernel/common/scheduler/control.c`

Удалить из `internal.h`:
```c
// extern thread_t* current_thread;  // убрать
```

В `control.c` заменить все прямые обращения к `current_thread` на
`thread_get_current()`. Целевые места: `scheduler_block`,
`scheduler_exit_current`, `scheduler_wake`.

В `task.c` в `thread_trampoline` заменить `current_thread` на `thread_get_current()`.

*Зависит от 0-C. Сложность: средняя. Риск: низкий на UP.*

---

#### P0-4: `task_destroy` уничтожает потоки задачи

**Файл:** `kernel/common/task.c` ~строки 263–288

После `task_registry_unlock`, перед fd-loop:
```c
/* Destroy all threads of this task except the currently running one.
 * The current thread is collected by the reaper after scheduler_exit_current(). */
{
    thread_t *th, *th_next;
    TAILQ_FOREACH_SAFE(th, &task->threads, task_link, th_next) {
        if (th == thread_get_current()) {
            continue;
        }
        thread_destroy(th);
    }
}
```

*Зависит от 0-C, 0-D. Сложность: малая. Риск: низкий.*

---

#### P2-10: Passthrough ошибок в `vfs_write` *(попутно)*

**Файл:** `kernel/fs/vfs.c` ~строки 885–889

```c
// Было:
if (wrc != RDNX_E_INVALID && wrc != RDNX_E_NOMEM) {
    return RDNX_E_UNSUPPORTED;
}
return wrc;

// Стало:
return wrc;  /* propagate ext2 error verbatim */
```

Проверить errno-маппинг в `kernel/unix/unix_fs.c` — добавить обработку
новых кодов если нужно.

*Сложность: тривиальная. Риск: низкий.*

---

### Phase 2 — P1 + P2 (дни 8–11)

---

#### P1-5a: Rollback при частичной аллокации стека в loader

**Файл:** `kernel/common/loader.c` ~строки 160–183

**Предварительно:** убедиться, что `paging_unmap_page_4kb_pml4` существует
в `kernel/arch/x86_64/paging.c`. Если нет — добавить первым делом.

В `loader_map_stack` при ошибке на итерации `i`:
```c
if (!phys || map_rc != RDNX_OK) {
    /* rollback: unmap and free pages 0..i-1 */
    for (uint32_t j = 0; j < i; j++) {
        paging_unmap_page_4kb_pml4(pml4_phys,
            out_img->stack_bottom + (uint64_t)j * USER_PAGE_SIZE);
        pmm_free_page(out_img->stack_phys[j]);
        out_img->stack_phys[j] = 0;
    }
    return RDNX_E_NOMEM;
}
```

В `loader_load_elf` при ошибке маппинга сегментов — итерироваться по уже
загруженным сегментам (`out->seg_count` уже инкрементирован внутри
`loader_map_segment`) и вызывать `paging_unmap + pmm_free`, затем
`paging_destroy_user_pml4(pml4_phys)`.

*Сложность: средняя. Риск: средний (зависит от наличия paging_unmap).*

---

#### P1-5b: Логирование ошибки rollback в `ext2_resize_file`

**Файл:** `kernel/fs/ext2.c` ~строки 1036–1038

```c
// Было:
(void)ext2_trim_inode_blocks(&g_ext2_live,
    (uint32_t)node->inode->fs_ino, &ino, old_blocks);

// Стало:
int trim_rc = ext2_trim_inode_blocks(&g_ext2_live,
    (uint32_t)node->inode->fs_ino, &ino, old_blocks);
if (trim_rc != RDNX_OK) {
    kprintf("[EXT2] resize rollback failed: alloc_err=%d trim_err=%d ino=%u\n",
            rc, trim_rc, (uint32_t)node->inode->fs_ino);
    /* Disk state is inconsistent for this inode. Return I/O error. */
}
return (rc != RDNX_OK) ? rc : RDNX_E_UNSUPPORTED;
```

*Сложность: малая. Риск: нулевой.*

---

#### P1-6 Part A: VFS cache — generation stamp на ноде

**Файл:** `kernel/fs/vfs.c`, `kernel/fs/vfs.h`

В `vfs_inode_t` добавить:
```c
uint32_t node_gen;
```

В `vfs_free_node`:
```c
if (node->inode) {
    node->inode->node_gen++;
}
```

В `vfs_cache_entry_t` добавить:
```c
uint32_t node_gen;
```

В `vfs_cache_insert`:
```c
entry.node_gen = node->inode ? node->inode->node_gen : 0;
```

В `vfs_cache_lookup` после сравнения пути:
```c
if (vfs_cache[i].node->inode &&
    vfs_cache[i].node->inode->node_gen != vfs_cache[i].node_gen) {
    continue;  /* node was freed and reallocated — treat as miss */
}
return vfs_cache[i].node;
```

Это вероятностная защита (generation wrap при ~4 млрд freed nodes),
но покрывает самый распространённый случай UAF.

*Сложность: малая. Риск: очень низкий.*

---

#### P2-9: Глубина priority inheritance → 8, флаг переполнения

**Файлы:** `kernel/core/task.h`, `kernel/common/scheduler/control.c`

В `task.h`:
```c
// Было:
int16_t inherit_stack[4];

// Стало:
int16_t inherit_stack[8];
uint8_t has_inherit_overflow;
```

Обновить инициализационные циклы в `thread_create` и `thread_create_user_clone`
с `< 4` на `< 8`.

В `scheduler_inherit_priority`:
```c
// Было:
if (target->inherit_depth < 4) { ... }

// Стало:
if (target->inherit_depth < 8) {
    target->inherit_stack[target->inherit_depth++] = target->inherited_priority;
} else {
    target->has_inherit_overflow = 1;
    kprintf("[SCHED] priority inheritance overflow on thread %u\n", target->id);
}
```

В `scheduler_clear_inherit`:
```c
if (target->has_inherit_overflow) {
    target->inherited_priority = target->base_priority;
    target->inherit_depth = 0;
    target->has_inherit_overflow = 0;
} else if (target->inherit_depth > 0) {
    target->inherited_priority = target->inherit_stack[--target->inherit_depth];
}
```

*Сложность: малая. Риск: очень низкий.*

---

### Phase 3 — P2: Edge cases (дни 12–16)

---

#### P2-8: `ext2_inode_size_bytes` — поддержка `size_high`

**Файл:** `kernel/fs/ext2.c` ~строки 128–132

```c
static uint64_t ext2_inode_size_bytes(const ext2_inode_t* ino)
{
    if (!ino) { return 0; }
    /*
     * size_high is valid for regular files in ext2 rev >= 1 with
     * RO_COMPAT_LARGE_FILE. For directories it is dir_acl — callers
     * check ext2_is_reg() before relying on the combined size.
     */
    uint64_t sz = (uint64_t)ino->size_lo | ((uint64_t)ino->size_high << 32);
    if (sz > (1ULL << 40)) {
        /* Sanity cap: 1 TB. Likely a corrupted inode. */
        kprintf("[EXT2] inode: suspicious size %llu, ignoring size_high\n",
                (unsigned long long)sz);
        return (uint64_t)ino->size_lo;
    }
    return sz;
}
```

Обновить пути записи в `ext2_writeback_file` и `ext2_resize_file`:
```c
ino.size_lo   = (uint32_t)(final_size & 0xFFFFFFFFu);
ino.size_high = (uint32_t)(final_size >> 32);
```

`EXT2_MAX_FILE_BYTES` (1 MB) по-прежнему ограничивает in-memory буфер в
`ext2_load_file` — это намеренно.

*Сложность: малая. Риск: низкий.*

---

#### P1-7: Документация локов (попутно с каждым PR)

При изменении каждого файла добавлять `LOCKING:`-комментарии к глобалам.

Шаблон для `ext2.c`:
```c
/* LOCKING: g_ext2_live.lock (spinlock_t in ext2_mount_ctx_t)
 *   Protects: все поля g_ext2_live, bitmap-операции,
 *             ext2_alloc_block, ext2_free_block,
 *             ext2_sync_super_and_gdt, ext2_writeback_file,
 *             ext2_resize_file.
 *   Lock order: ext2_lock -> queue->lock (в таком порядке,
 *               никогда в обратном).
 *   Callers: ext2_writeback_file, ext2_resize_file держат lock.
 *            ext2_alloc_block, ext2_free_block — caller holds.
 */
```

---

### Phase 4 — Будущее: Полноценный VFS refcounting (P1-6 Part B)

Большая задача, требует отдельного дизайна. Выполняется после стабилизации
всего остального.

Суть изменений:

1. Добавить `uint32_t ref_count` в `vfs_node_t`.
2. Ввести `vfs_node_retain(node)` / `vfs_node_release(node)`.
3. `vfs_cache_lookup` возвращает ноду с `retain` — caller обязан вызвать
   `release` по завершении.
4. `vfs_cache_insert` вызывает `retain`.
5. `vfs_unlink` — `vfs_node_release`; фактическое освобождение происходит
   в `vfs_node_release` при `ref_count == 0`.
6. Все call-sites, получающие ноду из VFS, аудитируются на наличие `release`.

Зависит от Part A (gen stamp), который остаётся как вторая линия защиты.

---

## Сводная таблица

| # | Фикс | Файл | Сложность | Риск | Фаза |
|---|------|------|-----------|------|------|
| 0-A | spinlock в `ext2_mount_ctx_t` | `ext2.c` | малая | нулевой | 0 |
| 0-B | `g_port_table_lock` | `ipc.c` | малая | нулевой | 0 |
| 0-C | per-CPU locals array | `task.c` | средняя | низкий | 0 |
| 0-D | thread-list в `task_t` / `thread_t` | `task.h`, `task.c` | малая | низкий | 0 |
| P0-1 | ext2 writeback spinlock | `ext2.c` | средняя | средний | 1 |
| P0-2 | IPC refcount атомарен с push | `ipc.c` | средняя | средний | 1 |
| P0-3 | per-CPU аксессоры в scheduler | `task.c`, `control.c` | средняя | средний | 1 |
| P0-4 | `task_destroy` → `thread_destroy` | `task.c` | малая | низкий | 1 |
| P2-10 | vfs_write error passthrough | `vfs.c` | тривиальная | низкий | 1 |
| P1-5a | loader stack rollback | `loader.c` | средняя | средний | 2 |
| P1-5b | ext2 resize rollback log | `ext2.c` | малая | нулевой | 2 |
| P1-6A | VFS cache gen stamp | `vfs.c`, `vfs.h` | малая | низкий | 2 |
| P2-9 | inherit_stack depth 8 + overflow flag | `task.h`, `control.c` | малая | низкий | 2 |
| P2-8 | ext2 size_high | `ext2.c` | малая | низкий | 3 |
| P1-7 | locking documentation | все | малая | нулевой | попутно |
| P1-6B | VFS full refcounting | `vfs.c`, `vfs.h` | большая | высокий | 4 |

---

## Реестр рисков

| Фикс | Риск регрессии | Комментарий |
|------|----------------|-------------|
| P0-1 (ext2 lock) | Средний | Block I/O сериализован; проверить что bdev не реентерабелен через ext2 |
| P0-2 (IPC refcount) | Средний | Порядок локов: port_table → queue; задокументировать |
| P0-3 (per-CPU) | Низкий (UP) / Высокий (SMP) | `cpu_id()` должен быть безопасен из interrupt context |
| P0-4 (thread destroy) | Низкий | Пропускать current_thread; reaper его соберёт |
| P1-5a (loader rollback) | Средний | Требует `paging_unmap`; тестировать через exec failure injection |
| P1-5b (resize log) | Нулевой | Только диагностика, нет поведенческих изменений |
| P1-6A (gen stamp) | Очень низкий | Вероятностная защита, не полная |
| P1-6B (ref count) | Высокий | Инвазивно; требует полного аудита call-sites |
| P2-8 (size_high) | Низкий | Добавить sanity cap; проверить corrupted inode scenario |
| P2-9 (inherit depth) | Очень низкий | Увеличить массив; обновить два init-loop |
| P2-10 (error passthrough) | Низкий | Аудит errno-маппинга в unix layer |
