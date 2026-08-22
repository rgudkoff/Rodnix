# Unified File Object Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Заменить `void* fd_table[] + uint8_t fd_kind[]` на refcount-описание открытого файла `rdnx_file_t` с диспетчеризацией через `file_ops_t`, чтобы новый тип дескриптора не требовал правок в слое дескрипторов.

**Architecture:** Вводится единый объект описания открытого файла с таблицей операций. Представление в таблице дескрипторов меняется один раз (Task 1), после чего syscall-обработчики переводятся с `switch` по типу на вызов ops группами (Tasks 3–5). Готовность (`poll`) строится по схеме BSD `selinfo`: объект будит зарегистрированных ожидающих, поток спит на своей единственной очереди — это обходит ограничение планировщика «один `waitq_owner` на поток».

**Tech Stack:** C (freestanding, `x86_64-elf-gcc`), сборка через `make`, тесты — контрактные проверки в `userland/init/init.c`, прогон в QEMU через `scripts/ci/contract_qemu.sh`.

**Spec:** `docs/superpowers/specs/2026-08-22-unified-file-object-design.md`

## Global Constraints

- Язык ядра — C. Стиль существующих файлов: 4 пробела, `snake_case`, префикс `rdnx_`/подсистемный, комментарии `/* */`.
- Коды возврата — `RDNX_*` из `include/error.h`. Расширение набора кодов **вне области** этого плана.
- `ssize_t` в дереве отсутствует. Возврат размеров — `int64_t`, отрицательное значение есть `RDNX_E_*`.
- `spinlock_t` — из `kernel/fabric/spin.h` (`spinlock_init/lock/unlock/trylock`).
- `TAILQ_*` — из `include/bsd/sys/queue.h`.
- **Таблица дескрипторов живёт в UNIX-персоналии, а не в задаче.** `task_t`
  (`kernel/core/task.h`) — объект ядра; `proc_t` (`kernel/unix/proc.h`) — креды,
  дескрипторы, cwd, сигналы. Из задачи персоналия достаётся через
  `task_proc(task)`, для текущей задачи есть `proc_current()`; обе могут вернуть
  `NULL`, и это допустимое состояние. Файлы, не включающие `kernel/unix/proc.h`,
  к полям дескрипторов обратиться не могут — граница проверяется компилятором.
- `KERNEL_STACK_SIZE` = 32 KiB (`kernel/task.c:20`). Крупные локальные массивы в syscall-пути запрещены.
- `POLL_MAX_REGS` = 256, `POLL_INLINE_REGS` = 8.
- Начальная ёмкость таблицы дескрипторов 32, потолок 1024.
- Новые файлы ядра регистрируются в `KERNEL_C_SRCS` соответствующего `Makefile` подсистемы (`kernel/Makefile`, `fs/Makefile`).
- **После каждой задачи дерево обязано загружаться до shell-приглашения.** Задача не считается завершённой, если `scripts/ci/contract_qemu.sh` не даёт `[CT] ALL PASS`.
- Контрактные проверки живут в `run_contract_mode_if_enabled()` в `userland/init/init.c` и логируются через `ct_log(id, verdict, msg)`. Занятые идентификаторы — до `CT-033` включительно; этот план использует `CT-034`…`CT-040`.

---

## Как гонять тесты

Полный контрактный прогон (сборка ядра, initrd, ISO, диска и запуск QEMU):

```bash
TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh
```

Успех — строка `[contract] contract markers detected: ALL PASS`. При провале скрипт печатает последние `[CT]`-маркеры из `boot.log`.

Быстрая проверка сборки без QEMU:

```bash
make clean && make
```

---

## Структура файлов

| файл | ответственность | статус |
|---|---|---|
| `include/sys/file.h` | `rdnx_file_t`, `file_ops_t`, публичный API объекта | создаётся Task 1 |
| `include/sys/selinfo.h` | `selinfo_t`, `sel_waiter_t`, `poll_ctx_t` | создаётся Task 5 |
| `kernel/unix/fd/file.c` | объект, refcount, таблица дескрипторов | создаётся Task 1 |
| `kernel/unix/fd/selinfo.c` | регистрация и пробуждение ожидающих | создаётся Task 5 |
| `fs/vfs_file.c` | `vfs_fileops` | создаётся Task 1 |
| `kernel/unix/fd/pipe.c` | `pipe_fileops`, `unix_pipe_t` | создаётся Task 3 (вынос из `unix_fd.c`) |
| `net/socket_file.c` | `socket_fileops` | создаётся Task 3 |
| `kernel/unix/fd/unix_fd.c` | только syscall-обвязка | сокращается Tasks 1–7 |
| `kernel/unix/proc.h` | `fd_table_t` вместо трёх плоских массивов в `proc_t` | правится Tasks 1, 6 |
| `kernel/unix/process/unix_proc.c` | `proc_fd_alloc/get/close`, рост таблицы | правится Tasks 1, 6 |
| `userland/init/init.c` | контрактные проверки `CT-034`…`CT-040` | правится Tasks 1–6 |

---

## Task 1: Объект описания и refcount

Меняется **представление**, не поведение отдельных операций: в `fd_table` теперь лежит `rdnx_file_t*` для всех типов. Тип временно хранится в поле `kind` внутри объекта — необращённые syscall-обработчики продолжают switch'иться по нему, читая `f->priv`. Поле удаляется в Task 7.

Одновременно `pos` переезжает в описание, а `dup`/`fork` переходят на refcount — это и есть наблюдаемое изменение, которое проверяют тесты.

**Files:**
- Create: `include/sys/file.h`
- Create: `kernel/unix/fd/file.c`
- Create: `fs/vfs_file.c`
- Modify: `kernel/Makefile` (добавить `kernel/unix/fd/file.c`)
- Modify: `fs/Makefile` (добавить `fs/vfs_file.c`)
- Modify: `kernel/unix/proc.h:81-83`
- Modify: `kernel/unix/process/unix_proc.c:155-191`
- Modify: `kernel/unix/fd/unix_fd.c` (`unix_fd_release`, `unix_fd_dup_into`, `unix_clone_fds_for_spawn`, `unix_bind_fd_to_console`, `unix_fs_open*`, `unix_fs_read/write/lseek`)
- Test: `userland/init/init.c` (`run_contract_mode_if_enabled`)

**Interfaces:**
- Consumes: `vfs_open/vfs_close/vfs_read/vfs_write/vfs_seek` (`fs/vfs.h`), `kmalloc/kfree` (`lib/heap.h`), `spinlock_t` (`kernel/fabric/spin.h`).
- Produces:
  - `rdnx_file_t* rdnx_file_alloc(const file_ops_t* ops, void* priv);`
  - `void rdnx_file_ref(rdnx_file_t* f);`
  - `void rdnx_file_put(rdnx_file_t* f);`
  - `int fd_install(proc_t* proc, rdnx_file_t* f);`
  - `rdnx_file_t* fd_get(proc_t* proc, int fd);`
  - `void fd_put(rdnx_file_t* f);`
  - `int fd_close(proc_t* proc, int fd);`
  - `extern const file_ops_t vfs_fileops;`
  - `rdnx_file_t* vfs_file_open(const char* path, int vfs_flags);`

- [ ] **Step 1: Написать падающую контрактную проверку CT-034 (общий offset у dup)**

В `userland/init/init.c`, внутри `run_contract_mode_if_enabled()`, перед финальным `if (ok)`:

```c
    {
        /* CT-034: dup разделяет описание, значит и offset. */
        int local_ok = 1;
        char buf[8];
        long fd = posix_open("/mnt/ct_dupoff.txt", VFS_OPEN_WRITE | VFS_OPEN_CREATE);
        if (fd < 0) {
            local_ok = 0;
        } else {
            long fd2 = posix_dup((int)fd);
            if (fd2 < 0) {
                local_ok = 0;
            } else {
                /* Пишем через оба дескриптора. При общем описании получится
                 * "abcd"; при копирующем dup второй write затрёт первый. */
                if (posix_write((int)fd, "ab", 2) != 2) local_ok = 0;
                if (posix_write((int)fd2, "cd", 2) != 2) local_ok = 0;
                (void)posix_close((int)fd2);
            }
            (void)posix_close((int)fd);
        }
        if (local_ok) {
            long rfd = posix_open("/mnt/ct_dupoff.txt", VFS_OPEN_READ);
            if (rfd < 0) {
                local_ok = 0;
            } else {
                long n = posix_read((int)rfd, buf, sizeof(buf));
                (void)posix_close((int)rfd);
                if (n != 4 || buf[0] != 'a' || buf[1] != 'b' ||
                    buf[2] != 'c' || buf[3] != 'd') {
                    local_ok = 0;
                }
            }
        }
        (void)posix_unlink("/mnt/ct_dupoff.txt");

        if (local_ok) {
            ct_log("CT-034", "PASS", "dup shares file offset");
        } else {
            ct_log("CT-034", "FAIL", "dup does not share file offset");
            ok = 0;
        }
    }
```

- [ ] **Step 2: Написать падающую контрактную проверку CT-035 (dup сокета)**

Там же, следующим блоком:

```c
    {
        /* CT-035: сокет — обычное описание, dup обязан работать. */
        int local_ok = 1;
        long s = posix_socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 0);
        if (s < 0) {
            local_ok = 0;
        } else {
            long s2 = posix_dup((int)s);
            if (s2 < 0) {
                local_ok = 0;
            } else {
                /* Закрытие одной копии не должно убивать вторую. */
                (void)posix_close((int)s);
                if (posix_close((int)s2) < 0) {
                    local_ok = 0;
                }
            }
            if (!local_ok) {
                (void)posix_close((int)s);
            }
        }
        if (local_ok) {
            ct_log("CT-035", "PASS", "dup works on socket fd");
        } else {
            ct_log("CT-035", "FAIL", "dup on socket fd failed");
            ok = 0;
        }
    }
```

- [ ] **Step 3: Написать падающую контрактную проверку CT-041 (fork разделяет описание)**

```c
    {
        /* CT-041: после fork описание общее. Закрытие дескриптора у родителя
         * не должно обрывать его у потомка. */
        int local_ok = 1;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            local_ok = 0;
        } else {
            long pid = posix_fork();
            if (pid == 0) {
                /* Потомок: родитель к этому моменту мог закрыть свой fd. */
                char b = 0;
                long r = posix_read((int)fd, &b, 1);
                posix_exit((r == 1) ? 0 : 1);
            } else if (pid < 0) {
                local_ok = 0;
                (void)posix_close((int)fd);
            } else {
                int status = 0;
                (void)posix_close((int)fd);   /* родитель закрывает первым */
                if (posix_waitpid(pid, &status) < 0 || status != 0) {
                    local_ok = 0;
                }
            }
        }
        if (local_ok) {
            ct_log("CT-041", "PASS", "fork shares the file description");
        } else {
            ct_log("CT-041", "FAIL", "child fd died with parent close");
            ok = 0;
        }
    }
```

Все три обёртки уже есть в `userland/include/posix_syscall.h`:
`posix_fork` (строка 393), `posix_exit` (283), `posix_waitpid` (319).
`posix_waitpid` принимает два аргумента — `(long pid, int* status)`,
третьего параметра флагов у неё нет.

- [ ] **Step 4: Прогнать тесты и убедиться, что новые проверки падают**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: FAIL. В `boot.log` — `[CT] CT-034 FAIL`, `[CT] CT-035 FAIL` и `[CT] CT-041 FAIL`. Причины: `unix_fd_dup_into` копирует `vfs_file_t` (`kernel/unix/fd/unix_fd.c:376`), а для сокета возвращает `RDNX_E_UNSUPPORTED` (`unix_fd.c:413`).

- [ ] **Step 5: Создать `include/sys/file.h`**

```c
/**
 * @file file.h
 * @brief Единое описание открытого файла и таблица операций над ним.
 */

#ifndef _RODNIX_SYS_FILE_H
#define _RODNIX_SYS_FILE_H

#include <stddef.h>
#include <stdint.h>
#include "../../fs/vfs.h"      /* vfs_stat_t */

struct task;
typedef struct task task_t;

typedef struct rdnx_file rdnx_file_t;
struct poll_ctx;

/* Биты готовности. Раньше это был безымянный enum внутри unix_fd.c
 * (строка 51); теперь они часть контракта file_ops.poll, поэтому живут
 * рядом с ним. Значения не меняются — userland ABI прежний. */
enum {
    UNIX_POLLIN   = 0x0001,
    UNIX_POLLOUT  = 0x0004,
    UNIX_POLLERR  = 0x0008,
    UNIX_POLLHUP  = 0x0010,
    UNIX_POLLNVAL = 0x0020
};

typedef struct file_ops {
    const char* name;   /* "vfs" / "pipe" / "socket" — для отладки и fdinfo */
    int64_t (*read)  (rdnx_file_t* f, void* kbuf, size_t len);
    int64_t (*write) (rdnx_file_t* f, const void* kbuf, size_t len);
    int64_t (*seek)  (rdnx_file_t* f, int64_t off, int whence);
    int     (*ioctl) (rdnx_file_t* f, uint64_t req, void* karg);
    int     (*stat)  (rdnx_file_t* f, vfs_stat_t* out);
    short   (*poll)  (rdnx_file_t* f, short events, struct poll_ctx* pc);
    int     (*close) (rdnx_file_t* f);   /* только на последней ссылке */
} file_ops_t;

/* Временное поле kind: пока не все обработчики переведены на ops.
 * Удаляется в Task 7 вместе с UNIX_FD_KIND_*. */
struct rdnx_file {
    const file_ops_t* ops;
    uint32_t  refs;
    uint32_t  status_flags;   /* O_APPEND / O_NONBLOCK — свойство описания */
    int64_t   pos;
    void*     priv;           /* vfs_file_t* / unix_pipe_t* / net_socket_t* */
    uint8_t   kind;           /* TEMP: UNIX_FD_KIND_* на время миграции */
};

/* Статусные флаги описания. Значения совпадают с UNIX_FD_NONBLOCK,
 * чтобы перенос из fd_flags был механическим. */
#define RDNX_F_NONBLOCK  0x01u
#define RDNX_F_APPEND    0x02u

rdnx_file_t* rdnx_file_alloc(const file_ops_t* ops, void* priv);
void         rdnx_file_ref(rdnx_file_t* f);
void         rdnx_file_put(rdnx_file_t* f);

int          fd_install(proc_t* proc, rdnx_file_t* f);
rdnx_file_t* fd_get(proc_t* proc, int fd);
void         fd_put(rdnx_file_t* f);
int          fd_close(proc_t* proc, int fd);

#endif /* _RODNIX_SYS_FILE_H */
```

- [ ] **Step 6: Создать `kernel/unix/fd/file.c`**

```c
/**
 * @file file.c
 * @brief Объект описания открытого файла: аллокация и подсчёт ссылок.
 */

#include "../../../include/sys/file.h"
#include "../../../include/error.h"
#include "../proc.h"
#include "../../fabric/spin.h"
#include "../../../lib/heap.h"
#include <string.h>

rdnx_file_t* rdnx_file_alloc(const file_ops_t* ops, void* priv)
{
    if (!ops) {
        return NULL;
    }
    rdnx_file_t* f = (rdnx_file_t*)kmalloc(sizeof(rdnx_file_t));
    if (!f) {
        return NULL;
    }
    memset(f, 0, sizeof(*f));
    f->ops  = ops;
    f->priv = priv;
    f->refs = 1;
    return f;
}

void rdnx_file_ref(rdnx_file_t* f)
{
    if (f) {
        f->refs++;
    }
}

void rdnx_file_put(rdnx_file_t* f)
{
    if (!f || f->refs == 0) {
        return;
    }
    if (--f->refs > 0) {
        return;
    }
    if (f->ops && f->ops->close) {
        (void)f->ops->close(f);
    }
    kfree(f);
}

int fd_install(proc_t* proc, rdnx_file_t* f)
{
    if (!proc || !f) {
        return RDNX_E_INVALID;
    }
    return proc_fd_alloc(proc, f);
}

rdnx_file_t* fd_get(proc_t* proc, int fd)
{
    rdnx_file_t* f = (rdnx_file_t*)proc_fd_get(proc, fd);
    if (f) {
        rdnx_file_ref(f);
    }
    return f;
}

void fd_put(rdnx_file_t* f)
{
    rdnx_file_put(f);
}

int fd_close(proc_t* proc, int fd)
{
    rdnx_file_t* f = (rdnx_file_t*)proc_fd_get(proc, fd);
    if (!f) {
        return RDNX_E_INVALID;
    }
    (void)proc_fd_close(proc, fd);
    rdnx_file_put(f);
    return RDNX_OK;
}
```

- [ ] **Step 7: Создать `fs/vfs_file.c`**

`pos` теперь ведёт `rdnx_file_t`. `vfs_file_t` внутри `priv` остаётся носителем `node`/`writable`/`open_flags`; его собственное поле `pos` синхронизируется перед каждым вызовом VFS и считывается обратно после.

```c
/**
 * @file vfs_file.c
 * @brief Реализация file_ops поверх VFS.
 */

#include "vfs.h"
#include "../include/sys/file.h"
#include "../include/error.h"
#include "../lib/heap.h"

static int64_t vfs_fop_read(rdnx_file_t* f, void* kbuf, size_t len)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    vf->pos = (size_t)f->pos;
    int ret = vfs_read(vf, kbuf, len);
    f->pos = (int64_t)vf->pos;
    return (int64_t)ret;
}

static int64_t vfs_fop_write(rdnx_file_t* f, const void* kbuf, size_t len)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    vf->pos = (size_t)f->pos;
    int ret = vfs_write(vf, kbuf, len);
    f->pos = (int64_t)vf->pos;
    return (int64_t)ret;
}

static int64_t vfs_fop_seek(rdnx_file_t* f, int64_t off, int whence)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    uint64_t out = 0;
    vf->pos = (size_t)f->pos;
    int ret = vfs_seek(vf, off, whence, &out);
    if (ret != RDNX_OK) {
        return (int64_t)ret;
    }
    f->pos  = (int64_t)out;
    vf->pos = (size_t)out;
    return (int64_t)out;
}

static int vfs_fop_stat(rdnx_file_t* f, vfs_stat_t* out)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_E_INVALID;
    }
    return vfs_fstat(vf, out);
}

static int vfs_fop_close(rdnx_file_t* f)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf) {
        return RDNX_OK;
    }
    (void)vfs_close(vf);
    kfree(vf);
    f->priv = NULL;
    return RDNX_OK;
}

const file_ops_t vfs_fileops = {
    .name  = "vfs",
    .read  = vfs_fop_read,
    .write = vfs_fop_write,
    .seek  = vfs_fop_seek,
    .ioctl = NULL,     /* Task 4 */
    .stat  = vfs_fop_stat,
    .poll  = NULL,     /* Task 5 */
    .close = vfs_fop_close,
};

rdnx_file_t* vfs_file_open(const char* path, int vfs_flags)
{
    vfs_file_t* vf = (vfs_file_t*)kmalloc(sizeof(vfs_file_t));
    if (!vf) {
        return NULL;
    }
    if (vfs_open(path, vfs_flags, vf) != RDNX_OK) {
        kfree(vf);
        return NULL;
    }
    rdnx_file_t* f = rdnx_file_alloc(&vfs_fileops, vf);
    if (!f) {
        (void)vfs_close(vf);
        kfree(vf);
        return NULL;
    }
    f->pos = (int64_t)vf->pos;
    return f;
}
```

Объявить в `fs/vfs.h`, в конце, перед `#endif`:

```c
struct rdnx_file;
struct rdnx_file* vfs_file_open(const char* path, int vfs_flags);
```

- [ ] **Step 8: Зарегистрировать новые файлы в сборке**

В `fs/Makefile` дописать в список:

```make
	fs/vfs_file.c
```

В `kernel/Makefile`, рядом с `kernel/unix/fd/unix_fd.c` (строка 13):

```make
	kernel/unix/fd/file.c \
```

- [ ] **Step 9: Перевести хранение и очистку дескрипторов**

В `kernel/unix/proc.h:81-83` заменить комментарий к `fd_table`, тип оставить `void*` (в таблице теперь лежит `rdnx_file_t*`):

```c
    void* fd_table[PROC_MAX_FD];   /* rdnx_file_t* — описание открытого файла */
    uint8_t fd_flags[PROC_MAX_FD]; /* FD_CLOEXEC — свойство дескриптора */
```

Удалить строку `uint8_t fd_kind[PROC_MAX_FD];` и все обращения к `proc->fd_kind` заменить на `f->kind`.

В `kernel/unix/process/unix_proc.c` убрать `proc->fd_kind[i] = 0;` из `proc_fd_alloc` и `proc_fd_close`.

В `kernel/unix/fd/unix_fd.c` заменить `unix_fd_release` целиком:

```c
void unix_fd_release(task_t* task, int fd)
{
    proc_t* proc = task_proc(task);
    if (!proc || fd < 0 || fd >= PROC_MAX_FD) {
        return;
    }
    (void)fd_close(proc, fd);
}
```

И `unix_fd_dup_into` целиком:

```c
static int unix_fd_dup_into(task_t* task, int oldfd, int newfd)
{
    proc_t* proc = task_proc(task);
    if (!proc || oldfd < 0 || oldfd >= PROC_MAX_FD ||
        newfd < 0 || newfd >= PROC_MAX_FD) {
        return RDNX_E_INVALID;
    }
    rdnx_file_t* f = (rdnx_file_t*)proc->fd_table[oldfd];
    if (!f) {
        return RDNX_E_INVALID;
    }
    rdnx_file_ref(f);
    proc->fd_table[newfd] = f;
    proc->fd_flags[newfd] = 0;   /* FD_CLOEXEC не наследуется через dup */
    return RDNX_OK;
}
```

В `unix_clone_fds_for_spawn` заменить всё тело цикла копирования на:

```c
    const proc_t* pproc = task_proc(parent);
    proc_t* cproc = task_proc(child);
    if (!pproc || !cproc) {
        return RDNX_E_INVALID;
    }
    for (int fd = 0; fd < PROC_MAX_FD; fd++) {
        rdnx_file_t* f = (rdnx_file_t*)pproc->fd_table[fd];
        if (!f) {
            cproc->fd_table[fd] = NULL;
            cproc->fd_flags[fd] = 0;
            continue;
        }
        rdnx_file_ref(f);
        cproc->fd_table[fd] = f;
        cproc->fd_flags[fd] = pproc->fd_flags[fd];
    }
    return RDNX_OK;
```

В `unix_bind_fd_to_console` и во всех `unix_fs_open*` заменить пару «`kmalloc(vfs_file_t)` + `vfs_open`» на `vfs_file_open(path, flags)` и `proc_fd_alloc(proc, f)`; при неудаче — `rdnx_file_put(f)`.

Пометить тип: сразу после успешного `vfs_file_open` выставить `f->kind = UNIX_FD_KIND_VFS;`.

Для пайпа и сокета в этой задаче: там, где раньше в `fd_table` клался сырой `unix_pipe_t*`/`net_socket_t*`, теперь кладётся `rdnx_file_alloc(&pipe_fileops_stub, p)` / `rdnx_file_alloc(&socket_fileops_stub, sock)`, где stub-таблицы содержат только `close`:

```c
/* временно в unix_fd.c, переезжает в pipe.c / socket_file.c в Task 3 */
static int pipe_fop_close(rdnx_file_t* f)
{
    unix_pipe_release((unix_pipe_t*)f->priv, f->kind);
    f->priv = NULL;
    return RDNX_OK;
}
static const file_ops_t pipe_fileops_stub = {
    .name = "pipe", .close = pipe_fop_close
};

static int socket_fop_close(rdnx_file_t* f)
{
    net_socket_close((net_socket_t*)f->priv);
    f->priv = NULL;
    return RDNX_OK;
}
static const file_ops_t socket_fileops_stub = {
    .name = "socket", .close = socket_fop_close
};
```

Во всех оставшихся обработчиках, которые ещё switch'атся по типу, заменить `proc->fd_kind[fdi]` на `((rdnx_file_t*)h)->kind`, а рабочий указатель получать как `((rdnx_file_t*)h)->priv`.

В `unix_fs_read`/`unix_fs_write` для ветки VFS вызывать `f->ops->read(f, buf, n)` / `f->ops->write(f, buf, n)` — иначе `pos` из описания не будет использован и CT-034 не пройдёт. Ветки пайпа и сокета оставить как есть (переводятся в Task 3).

В `unix_fs_lseek` для VFS вызывать `f->ops->seek(f, off, whence)`.

- [ ] **Step 10: Собрать ядро**

Run: `make clean && make`
Expected: сборка без ошибок и без предупреждений о неиспользуемых функциях. Если линковщик ругается на `UNIX_FD_KIND_*` — остались необращённые обращения к `proc->fd_kind`, их надо найти через `grep -rn "fd_kind" kernel/`.

- [ ] **Step 11: Прогнать контрактный набор**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: PASS. `CT-034` и `CT-035` дают PASS, все ранее существовавшие `CT-*` остаются PASS, финальная строка `[CT] ALL PASS`.

- [ ] **Step 12: Коммит**

```bash
git add include/sys/file.h kernel/unix/fd/file.c fs/vfs_file.c fs/vfs.h \
        kernel/Makefile fs/Makefile kernel/unix/proc.h \
        kernel/unix/process/unix_proc.c \
        kernel/unix/fd/unix_fd.c userland/init/init.c
git commit -m "feat(fd): introduce refcounted rdnx_file_t description

fd_table now holds rdnx_file_t* for every descriptor type. dup and fork
share the description instead of copying it, which gives sockets a working
dup and makes the file offset shared as POSIX requires.

Type dispatch still switches on a temporary rdnx_file.kind field; it is
removed in the final cleanup task."
```

---

## Task 2: Согласование с `vfs_node.ref_count`

После Task 1 `vfs_close` вызывается один раз на описание, а не на каждый дескриптор. Надо убедиться, что учёт ссылок на ноду сошёлся, и закрепить это тестом.

**Files:**
- Modify: `fs/vfs.c` (`vfs_open`, `vfs_close`, `vfs_file_dup`, `vfs_unlink`)
- Modify: `fs/vfs_file.c` при необходимости
- Test: `userland/init/init.c`

**Interfaces:**
- Consumes: `rdnx_file_t` (Task 1), `vfs_node_t.ref_count` / `.unlinked` (`fs/vfs.h:56`).
- Produces: инвариант — `vfs_node.ref_count` равен числу живых `rdnx_file_t`, ссылающихся на ноду, плюс единица за ссылку из дерева имён пока `unlinked == false`.

- [ ] **Step 1: Написать падающую контрактную проверку CT-036 (unlink открытого файла)**

В `userland/init/init.c`:

```c
    {
        /* CT-036: unlink открытого файла не рвёт чтение через живой fd. */
        int local_ok = 1;
        char buf[8];
        long wfd = posix_open("/mnt/ct_unlink.txt", VFS_OPEN_WRITE | VFS_OPEN_CREATE);
        if (wfd < 0 || posix_write((int)wfd, "live", 4) != 4) {
            local_ok = 0;
        }
        if (wfd >= 0) {
            (void)posix_close((int)wfd);
        }

        if (local_ok) {
            long rfd = posix_open("/mnt/ct_unlink.txt", VFS_OPEN_READ);
            if (rfd < 0) {
                local_ok = 0;
            } else {
                if (posix_unlink("/mnt/ct_unlink.txt") < 0) {
                    local_ok = 0;
                }
                /* Имя удалено, но описание живо — данные обязаны читаться. */
                long n = posix_read((int)rfd, buf, sizeof(buf));
                if (n != 4 || buf[0] != 'l' || buf[3] != 'e') {
                    local_ok = 0;
                }
                (void)posix_close((int)rfd);
                /* Повторное открытие обязано провалиться. */
                long again = posix_open("/mnt/ct_unlink.txt", VFS_OPEN_READ);
                if (again >= 0) {
                    (void)posix_close((int)again);
                    local_ok = 0;
                }
            }
        }

        if (local_ok) {
            ct_log("CT-036", "PASS", "unlinked file readable via open fd");
        } else {
            ct_log("CT-036", "FAIL", "unlink broke open fd");
            ok = 0;
        }
    }
```

- [ ] **Step 2: Прогнать и зафиксировать результат**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: `CT-036` даёт FAIL либо ядро паникует на освобождении ноды. Если проверка неожиданно проходит — учёт уже сошёлся; всё равно выполнить Step 3 (аудит) и Step 5, чтобы инвариант был зафиксирован в комментарии и в тесте.

- [ ] **Step 3: Провести аудит счётчика**

Прочитать в `fs/vfs.c` все места изменения `ref_count`:

```bash
grep -n "ref_count" fs/vfs.c fs/vfs.h
```

Проверить по каждому: инкремент происходит ровно в `vfs_open` (одна ссылка на одно описание), декремент ровно в `vfs_close`. `vfs_file_dup` после Task 1 больше не вызывается из слоя дескрипторов — убедиться в этом (`grep -rn "vfs_file_dup" --exclude-dir=.git .`) и, если вызовов не осталось, удалить функцию из `fs/vfs.c` и объявление из `fs/vfs.h`.

- [ ] **Step 4: Привести учёт к инварианту**

В `fs/vfs.c` над определением `vfs_node_t`-освобождающей функции добавить комментарий с инвариантом и исправить найденные расхождения:

```c
/*
 * Инвариант ссылок на ноду:
 *   ref_count == (число живых rdnx_file_t, ссылающихся на ноду)
 *                + (1, пока unlinked == false)
 *
 * vfs_open   : +1  (одно описание — одна ссылка)
 * vfs_close  : -1
 * vfs_unlink : снимает ссылку дерева, выставляет unlinked = true
 * Нода освобождается, когда ref_count достигает нуля.
 */
```

- [ ] **Step 5: Прогнать контрактный набор**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: PASS, включая `CT-036`.

- [ ] **Step 6: Коммит**

```bash
git add fs/vfs.c fs/vfs.h fs/vfs_file.c userland/init/init.c
git commit -m "fix(vfs): reconcile node refcount with file descriptions

vfs_close now runs once per description rather than once per descriptor,
so the node reference count is redefined in those terms and vfs_file_dup,
which no longer has callers, is removed."
```

---

## Task 3: `read` / `write` / `close` через ops

Три реализации переезжают к своим подсистемам. Из `unix_fs_read`/`unix_fs_write` исчезает диспетчеризация по типу.

**Files:**
- Create: `kernel/unix/fd/pipe.c`
- Create: `kernel/unix/fd/pipe.h`
- Create: `net/socket_file.c`
- Create: `net/socket_file.h`
- Modify: `kernel/Makefile`
- Modify: `kernel/unix/fd/unix_fd.c` (`unix_fs_read`, `unix_fs_write`, `unix_fs_pipe*`, `unix_fs_socket`, удаление stub-таблиц из Task 1)
- Test: `userland/init/init.c`

**Interfaces:**
- Consumes: `rdnx_file_t`, `file_ops_t`, `rdnx_file_alloc` (Task 1); `net_socket_tcp_recv/tcp_send/close` (`net/socket.h`).
- Produces:
  - `extern const file_ops_t pipe_fileops;` (`kernel/unix/fd/pipe.h`)
  - `int unix_pipe_create_pair(rdnx_file_t** out_r, rdnx_file_t** out_w);`
  - `extern const file_ops_t socket_fileops;` (`net/socket_file.h`)
  - `rdnx_file_t* socket_file_create(int domain, int type, int protocol);`
  - `net_socket_t* socket_file_sock(rdnx_file_t* f);` — для `bind`/`connect`/`listen`/`accept`, которые остаются socket-специфичными syscall'ами

- [ ] **Step 1: Написать падающую контрактную проверку CT-037 (обмен через сокет по read/write)**

```c
    {
        /* CT-037: сокет читается и пишется обычными read/write,
         * то есть проходит через ops, а не через socket-специальный путь. */
        int local_ok = 1;
        long s = posix_socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 0);
        if (s < 0) {
            local_ok = 0;
        } else {
            char b = 0;
            /* На несоединённом UDP-сокете read обязан вернуть ошибку,
             * а не POLLNVAL-подобный отказ уровня таблицы дескрипторов. */
            long r = posix_read((int)s, &b, 1);
            if (r >= 0) {
                local_ok = 0;
            }
            if (posix_close((int)s) < 0) {
                local_ok = 0;
            }
        }
        if (local_ok) {
            ct_log("CT-037", "PASS", "socket fd goes through file ops");
        } else {
            ct_log("CT-037", "FAIL", "socket fd read/close path broken");
            ok = 0;
        }
    }
```

- [ ] **Step 2: Прогнать и убедиться в текущем состоянии**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: `CT-037` может дать PASS уже сейчас (текущий socket-путь возвращает ошибку по другой причине — `net_socket_tcp_recv` с таймаутом 5 с). Записать наблюдаемое время прогона: после перевода на ops чтение должно возвращаться сразу, без пятисекундной паузы. Если суммарное время контрактного прогона выросло — это и есть сигнал, что старый путь ещё активен.

- [ ] **Step 3: Создать `kernel/unix/fd/pipe.h`**

```c
#ifndef _RODNIX_UNIX_PIPE_H
#define _RODNIX_UNIX_PIPE_H

#include "../../../include/sys/file.h"

extern const file_ops_t pipe_fileops;

/* Создаёт пару описаний одного пайпа. При успехе оба ненулевые,
 * refs каждого равен 1. */
int unix_pipe_create_pair(rdnx_file_t** out_r, rdnx_file_t** out_w);

#endif /* _RODNIX_UNIX_PIPE_H */
```

- [ ] **Step 4: Создать `kernel/unix/fd/pipe.c`**

Перенести из `kernel/unix/fd/unix_fd.c` без изменения логики: `unix_pipe_t`, `UNIX_PIPE_MAGIC`, `UNIX_PIPE_CAP`, `unix_pipe_lock/unlock`, `unix_pipe_retain/release`, тела чтения и записи из `unix_fs_read`/`unix_fs_write`. Направление конца пайпа хранить в самом описании:

```c
/* Направление конца хранится в status_flags описания, а не в fd_kind. */
#define PIPE_END_READ   0x100u
#define PIPE_END_WRITE  0x200u

static int64_t pipe_fop_read(rdnx_file_t* f, void* kbuf, size_t len)
{
    if (!(f->status_flags & PIPE_END_READ)) {
        return RDNX_E_INVALID;
    }
    unix_pipe_t* p = (unix_pipe_t*)f->priv;
    if (!p || p->magic != UNIX_PIPE_MAGIC) {
        return RDNX_E_INVALID;
    }

    uint8_t* out = (uint8_t*)kbuf;
    size_t done = 0;
    while (done < len) {
        uint32_t count, writers;
        uint8_t ch = 0;
        bool have_byte = false;

        irql_t old = unix_pipe_lock();
        count = p->count;
        writers = p->writers;
        if (count > 0) {
            ch = p->data[p->tail];
            p->tail = (p->tail + 1u) % UNIX_PIPE_CAP;
            p->count--;
            have_byte = true;
        }
        unix_pipe_unlock(old);

        if (have_byte) {
            out[done++] = ch;
            continue;
        }
        if (writers == 0 || done > 0) {
            break;
        }
        if (f->status_flags & RDNX_F_NONBLOCK) {
            return RDNX_E_AGAIN;
        }
        scheduler_yield();   /* заменяется на sel-ожидание в Task 5 */
    }
    return (int64_t)done;
}
```

`pipe_fop_write` переносится симметрично из соответствующей ветки `unix_fs_write`. `pipe_fop_close` вызывает `unix_pipe_release` с направлением из `status_flags`. Таблица:

```c
const file_ops_t pipe_fileops = {
    .name  = "pipe",
    .read  = pipe_fop_read,
    .write = pipe_fop_write,
    .seek  = NULL,
    .ioctl = NULL,
    .stat  = NULL,
    .poll  = NULL,     /* Task 5 */
    .close = pipe_fop_close,
};
```

`unix_pipe_create_pair` выделяет один `unix_pipe_t`, ставит `readers = writers = 1` и создаёт два описания через `rdnx_file_alloc(&pipe_fileops, p)` с разными `status_flags`.

- [ ] **Step 5: Создать `net/socket_file.h` и `net/socket_file.c`**

```c
/* net/socket_file.h */
#ifndef _RODNIX_NET_SOCKET_FILE_H
#define _RODNIX_NET_SOCKET_FILE_H

#include "../include/sys/file.h"
#include "socket.h"

extern const file_ops_t socket_fileops;

rdnx_file_t*  socket_file_create(int domain, int type, int protocol);
/* Возвращает NULL, если описание не является сокетом. */
net_socket_t* socket_file_sock(rdnx_file_t* f);

#endif /* _RODNIX_NET_SOCKET_FILE_H */
```

```c
/* net/socket_file.c */
#include "socket_file.h"
#include "../include/error.h"

static int64_t sock_fop_read(rdnx_file_t* f, void* kbuf, size_t len)
{
    net_socket_t* s = (net_socket_t*)f->priv;
    if (!s) {
        return RDNX_E_INVALID;
    }
    uint64_t timeout = (f->status_flags & RDNX_F_NONBLOCK) ? 0u : 5000u;
    int ret = net_socket_tcp_recv(s, kbuf, len, timeout);
    return (ret >= 0) ? (int64_t)ret : (int64_t)RDNX_E_INVALID;
}

static int64_t sock_fop_write(rdnx_file_t* f, const void* kbuf, size_t len)
{
    net_socket_t* s = (net_socket_t*)f->priv;
    if (!s) {
        return RDNX_E_INVALID;
    }
    int ret = net_socket_tcp_send(s, kbuf, len);
    return (ret >= 0) ? (int64_t)ret : (int64_t)RDNX_E_INVALID;
}

static int sock_fop_close(rdnx_file_t* f)
{
    net_socket_close((net_socket_t*)f->priv);
    f->priv = NULL;
    return RDNX_OK;
}

const file_ops_t socket_fileops = {
    .name  = "socket",
    .read  = sock_fop_read,
    .write = sock_fop_write,
    .seek  = NULL,
    .ioctl = NULL,
    .stat  = NULL,
    .poll  = NULL,     /* Task 5 */
    .close = sock_fop_close,
};

rdnx_file_t* socket_file_create(int domain, int type, int protocol)
{
    net_socket_t* s = net_socket_create(domain, type, protocol);
    if (!s) {
        return NULL;
    }
    rdnx_file_t* f = rdnx_file_alloc(&socket_fileops, s);
    if (!f) {
        net_socket_close(s);
        return NULL;
    }
    return f;
}

net_socket_t* socket_file_sock(rdnx_file_t* f)
{
    if (!f || f->ops != &socket_fileops) {
        return NULL;
    }
    return (net_socket_t*)f->priv;
}
```

`socket_file_sock` — это то, чем `bind`/`connect`/`listen`/`accept` проверяют тип вместо `fd_kind`. Проверка идёт по указателю на ops-таблицу, а не по числовому тегу, и не требует общего перечисления типов.

- [ ] **Step 6: Упростить `unix_fs_read` и `unix_fs_write`**

```c
uint64_t unix_fs_read(uint64_t fd, uint64_t user_buf_ptr, uint64_t len)
{
    proc_t* proc = proc_current();
    if (!proc) {
        return (uint64_t)RDNX_E_INVALID;
    }
    void* buf = (void*)(uintptr_t)user_buf_ptr;
    size_t n = (size_t)len;
    if (!unix_user_range_ok(buf, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }

    rdnx_file_t* f = fd_get(proc, (int)fd);
    if (!f) {
        return (uint64_t)RDNX_E_INVALID;
    }
    int64_t ret = f->ops->read ? f->ops->read(f, buf, n)
                               : (int64_t)RDNX_E_UNSUPPORTED;
    fd_put(f);
    return (uint64_t)ret;
}
```

`unix_fs_write` — симметрично, через `f->ops->write`.

Замечание: буфер здесь всё ещё пользовательский. Перевод на copyin/copyout — отдельная работа, в этот план не входит (см. раздел «Вне области»); текущее поведение сохраняется без изменений.

- [ ] **Step 7: Переключить создание пайпов и сокетов**

В `unix_fs_pipe`/`unix_fs_pipe2` использовать `unix_pipe_create_pair` и `fd_install`. В `unix_fs_socket` использовать `socket_file_create`. Удалить из `unix_fd.c` stub-таблицы `pipe_fileops_stub`/`socket_fileops_stub`, определения `unix_pipe_t` и все перенесённые в `pipe.c` функции.

В `unix_fs_bind/connect/sendto/recvfrom/listen/accept` получать сокет через `socket_file_sock(f)`; `NULL` означает `RDNX_E_INVALID`.

- [ ] **Step 8: Зарегистрировать новые файлы**

В `kernel/Makefile`:

```make
	kernel/unix/fd/pipe.c \
	net/socket_file.c \
```

- [ ] **Step 9: Собрать и прогнать**

Run: `make clean && make && TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: PASS. `pipetest` и пайплайны в shell работают; `CT-037` PASS; общее время прогона не выросло.

- [ ] **Step 10: Коммит**

```bash
git add kernel/unix/fd/pipe.c kernel/unix/fd/pipe.h net/socket_file.c \
        net/socket_file.h kernel/Makefile kernel/unix/fd/unix_fd.c \
        userland/init/init.c
git commit -m "refactor(fd): move read/write/close behind file ops

Pipe and socket implementations move to their own subsystems. unix_fs_read
and unix_fs_write no longer know which kinds of descriptor exist; socket
syscalls identify their type by ops pointer instead of a numeric tag."
```

---

## Task 4: Метаданные и управление через ops

`lseek`, `fstat`, `fcntl`, `ioctl`, `ftruncate`, `fchmod`, `fchown` перестают проверять тип дескриптора.

**Files:**
- Modify: `kernel/unix/fd/unix_fd.c` (`unix_fs_lseek`, `unix_fs_ftruncate`, `unix_fs_ioctl`, `unix_fs_fstat`, `unix_fs_fchmod`, `unix_fs_fchown`, `unix_fs_fcntl`)
- Modify: `fs/vfs_file.c` (реализация `ioctl`)
- Test: `userland/init/init.c`

**Interfaces:**
- Consumes: `file_ops_t.seek/ioctl/stat` (Task 1), `fd_get`/`fd_put`.
- Produces: `O_NONBLOCK` живёт в `rdnx_file->status_flags`; `FD_CLOEXEC` — в `proc->fd_flags`.

- [ ] **Step 1: Написать падающую контрактную проверку CT-038 (O_NONBLOCK разделяется, FD_CLOEXEC — нет)**

```c
    {
        /* CT-038: O_NONBLOCK — свойство описания и виден через dup;
         * FD_CLOEXEC — свойство дескриптора и через dup не наследуется. */
        int local_ok = 1;
        long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
        if (fd < 0) {
            local_ok = 0;
        } else {
            long fd2 = posix_dup((int)fd);
            if (fd2 < 0) {
                local_ok = 0;
            } else {
                /* F_SETFL = 4, O_NONBLOCK = 0x800 в userland ABI */
                if (posix_fcntl((int)fd, 4, 0x800) < 0) {
                    local_ok = 0;
                }
                /* F_GETFL = 3 — флаг обязан быть виден на копии */
                long fl = posix_fcntl((int)fd2, 3, 0);
                if (fl < 0 || !(fl & 0x800)) {
                    local_ok = 0;
                }
                /* F_SETFD = 2, FD_CLOEXEC = 1 — только на своём дескрипторе */
                if (posix_fcntl((int)fd, 2, 1) < 0) {
                    local_ok = 0;
                }
                /* F_GETFD = 1 — на копии обязан быть сброшен */
                long fdflags = posix_fcntl((int)fd2, 1, 0);
                if (fdflags < 0 || (fdflags & 1)) {
                    local_ok = 0;
                }
                (void)posix_close((int)fd2);
            }
            (void)posix_close((int)fd);
        }
        if (local_ok) {
            ct_log("CT-038", "PASS", "status flags shared, fd flags are not");
        } else {
            ct_log("CT-038", "FAIL", "flag ownership wrong");
            ok = 0;
        }
    }
```

Перед запуском проверить, что `posix_fcntl` объявлен в `userland/include/posix_syscall.h`; если нет — добавить обёртку рядом с `posix_dup` по тому же образцу, через `POSIX_SYS_FCNTL`.

- [ ] **Step 2: Прогнать и убедиться, что проверка падает**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: `CT-038` FAIL — сейчас `O_NONBLOCK` хранится в `proc->fd_flags` как `UNIX_FD_NONBLOCK` и через `dup` не разделяется.

- [ ] **Step 3: Реализовать `ioctl` в `vfs_fileops`**

В `fs/vfs_file.c` добавить и вписать в таблицу как `.ioctl = vfs_fop_ioctl`:

```c
static int vfs_fop_ioctl(rdnx_file_t* f, uint64_t req, void* karg)
{
    vfs_file_t* vf = (vfs_file_t*)f->priv;
    if (!vf || !vf->node || !vf->node->inode) {
        return RDNX_E_INVALID;
    }
    /* Существующая обработка запросов перенесена из unix_fs_ioctl:
     * termios на VFS_INODE_CONSOLE, размеры на VFS_INODE_FRAMEBUFFER.
     * Диспетчеризация по inode->flags остаётся внутри VFS — её снимает
     * следующая спека (второй слой), не этот план. */
    return vfs_ioctl_node(vf, req, karg);
}
```

Тело `vfs_ioctl_node` — это перенесённое без изменений содержимое `unix_fs_ioctl` из `kernel/unix/fd/unix_fd.c:1094`, за вычетом получения дескриптора и проверок пользовательских указателей, которые остаются в обвязке. Функцию объявить в `fs/vfs.h`.

- [ ] **Step 4: Перевести обработчики**

Каждый из семи обработчиков приводится к одной форме — на примере `unix_fs_fstat`:

```c
uint64_t unix_fs_fstat(uint64_t fd, uint64_t user_stat_ptr)
{
    proc_t* proc = proc_current();
    if (!proc) {
        return (uint64_t)RDNX_E_INVALID;
    }
    vfs_stat_t st;
    rdnx_file_t* f = fd_get(proc, (int)fd);
    if (!f) {
        return (uint64_t)RDNX_E_INVALID;
    }
    int ret = f->ops->stat ? f->ops->stat(f, &st) : RDNX_E_UNSUPPORTED;
    fd_put(f);
    if (ret != RDNX_OK) {
        return (uint64_t)ret;
    }
    if (unix_copy_to_user((void*)(uintptr_t)user_stat_ptr, &st, sizeof(st)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }
    return (uint64_t)RDNX_OK;
}
```

В `unix_fs_fcntl`: `F_GETFD`/`F_SETFD` работают с `proc->fd_flags[fd]`; `F_GETFL`/`F_SETFL` — с `f->status_flags`. Заменить все чтения `proc->fd_flags[fdi] & UNIX_FD_NONBLOCK` по дереву на `f->status_flags & RDNX_F_NONBLOCK`:

```bash
grep -rn "UNIX_FD_NONBLOCK" --exclude-dir=.git .
```

- [ ] **Step 5: Собрать и прогнать**

Run: `make clean && make && TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: PASS, включая `CT-038`.

- [ ] **Step 6: Коммит**

```bash
git add kernel/unix/fd/unix_fd.c fs/vfs_file.c fs/vfs.h \
        userland/include/posix_syscall.h userland/init/init.c
git commit -m "refactor(fd): route metadata syscalls through file ops

lseek, fstat, fcntl, ioctl, ftruncate, fchmod and fchown stop inspecting
descriptor kind. O_NONBLOCK moves to the shared description where dup can
see it; FD_CLOEXEC stays on the descriptor."
```

---

## Task 5: Готовность вместо busy-wait

Самая содержательная задача. `poll`/`select` перестают крутить цикл с `scheduler_yield()`.

**Files:**
- Create: `include/sys/selinfo.h`
- Create: `kernel/unix/fd/selinfo.c`
- Modify: `kernel/Makefile`
- Modify: `kernel/unix/fd/pipe.c` (`selinfo` в `unix_pipe_t`, `pipe_fop_poll`, пробуждения)
- Modify: `net/socket_file.c` (`sock_fop_poll`)
- Modify: `net/socket.c` (`selinfo` в `net_socket_t`, `sel_wakeup` на приходе данных и на постановке в accept-очередь)
- Modify: `fs/vfs_file.c` (`vfs_fop_poll`)
- Modify: `kernel/unix/fd/unix_fd.c` (`unix_fs_poll`, `unix_fs_select`, удаление `unix_poll_one`)
- Test: `userland/init/init.c`

**Interfaces:**
- Consumes: `waitq_wait_until`, `scheduler_wake`, `scheduler_get_ticks` (`sched/waitq.h`, `sched/scheduler.h`); `thread_t` (`kernel/core/task.h`).
- Produces:
  - `void selinfo_init(selinfo_t* si);`
  - `void sel_record(selinfo_t* si, poll_ctx_t* pc);`
  - `void sel_wakeup(selinfo_t* si);` — **вызываема из контекста прерывания**
  - `int  poll_begin(poll_ctx_t* pc, uint32_t nfds);`
  - `int  poll_sleep(poll_ctx_t* pc, uint64_t deadline_ticks);` — `RDNX_OK` / `RDNX_E_TIMEOUT`
  - `void poll_end(poll_ctx_t* pc);`

- [ ] **Step 1: Написать падающую контрактную проверку CT-039 (poll не жжёт CPU)**

```c
    {
        /* CT-039: poll с таймаутом спит, а не крутится.
         * Пайп без писателей в него — событий нет, ждём весь таймаут. */
        int local_ok = 1;
        int pfd[2];
        if (posix_pipe(pfd) < 0) {
            local_ok = 0;
        } else {
            struct { int fd; short events; short revents; } fds[1];
            fds[0].fd = pfd[0];
            fds[0].events = 1 /* POLLIN */;
            fds[0].revents = 0;

            long before = ct_self_cpu_ticks();
            long r = posix_poll(fds, 1, 300);
            long after = ct_self_cpu_ticks();

            /* Таймаут обязан истечь без событий. */
            if (r != 0) {
                local_ok = 0;
            }
            /* Тик планировщика — 10 мс (SCHEDULER_TIME_SLICE_MS).
             * 300 мс busy-wait дают порядка 30 тиков CPU, нормальный сон —
             * ноль или единицы. Порог с запасом. */
            if (before < 0 || after < 0 || after - before > 5) {
                local_ok = 0;
            }
            (void)posix_close(pfd[0]);
            (void)posix_close(pfd[1]);
        }
        if (local_ok) {
            ct_log("CT-039", "PASS", "poll sleeps instead of spinning");
        } else {
            ct_log("CT-039", "FAIL", "poll burns CPU or wrong return");
            ok = 0;
        }
    }
```

Вспомогательная функция рядом с `ct_log` в `userland/init/init.c`. Менять
`fs/procfs.c` не нужно: `/proc/self/stat` уже отдаёт накопленные тики CPU
последним полем строки формата `pid (command) state ppid uid nthreads cputicks`
(`fs/procfs.c:357`).

```c
/* Накопленные тики CPU текущей задачи — последнее поле /proc/self/stat.
 * Возвращает -1 при ошибке. */
static long ct_self_cpu_ticks(void)
{
    char buf[256];
    long fd = posix_open("/proc/self/stat", VFS_OPEN_READ);
    if (fd < 0) {
        return -1;
    }
    long n = posix_read((int)fd, buf, (uint64_t)sizeof(buf) - 1);
    (void)posix_close((int)fd);
    if (n <= 0) {
        return -1;
    }
    buf[n] = 0;

    /* Отмотать назад от конца строки до последней группы цифр. */
    long i = n - 1;
    while (i >= 0 && (buf[i] == '\n' || buf[i] == ' ')) {
        i--;
    }
    long end = i;
    while (i >= 0 && buf[i] >= '0' && buf[i] <= '9') {
        i--;
    }
    if (i == end) {
        return -1;
    }

    long val = 0;
    for (long k = i + 1; k <= end; k++) {
        val = val * 10 + (buf[k] - '0');
    }
    return val;
}
```

- [ ] **Step 2: Прогнать и убедиться, что проверка падает**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: `CT-039` FAIL по превышению порога CPU — текущий `unix_fs_poll` крутит `scheduler_yield()` весь таймаут.

- [ ] **Step 3: Создать `include/sys/selinfo.h`**

```c
/**
 * @file selinfo.h
 * @brief Регистрация ожидающих готовности (схема BSD selinfo).
 *
 * Поток может спать только на одной waitq (см. thread_t.waitq_owner),
 * а poll ждёт на N объектах. Поэтому ожидающий спит на своей очереди,
 * а объект, ставший готовым, будит зарегистрированных на нём.
 */

#ifndef _RODNIX_SYS_SELINFO_H
#define _RODNIX_SYS_SELINFO_H

#include <stdint.h>
#include "../bsd/sys/queue.h"
#include "../../kernel/fabric/spin.h"

struct thread;
typedef struct thread thread_t;

struct selinfo;
struct poll_ctx;

typedef struct sel_waiter {
    thread_t*        thread;
    struct poll_ctx* pc;
    struct selinfo*  si;
    TAILQ_ENTRY(sel_waiter) si_link;
} sel_waiter_t;

typedef struct selinfo {
    TAILQ_HEAD(sel_waiter_head, sel_waiter) waiters;
    spinlock_t lock;
} selinfo_t;

#define POLL_MAX_REGS     256u   /* потолок nfds */
#define POLL_INLINE_REGS  8u     /* типичный случай — без аллокации */

typedef struct poll_ctx {
    sel_waiter_t  inline_regs[POLL_INLINE_REGS];
    sel_waiter_t* regs;          /* == inline_regs при cap <= POLL_INLINE_REGS */
    uint32_t      cap;
    uint32_t      n;
    volatile int  woken;
} poll_ctx_t;

void selinfo_init(selinfo_t* si);
void sel_record(selinfo_t* si, poll_ctx_t* pc);
void sel_wakeup(selinfo_t* si);   /* вызываема из контекста прерывания */

int  poll_begin(poll_ctx_t* pc, uint32_t nfds);
int  poll_sleep(poll_ctx_t* pc, uint64_t deadline_ticks);
void poll_end(poll_ctx_t* pc);

#endif /* _RODNIX_SYS_SELINFO_H */
```

- [ ] **Step 4: Создать `kernel/unix/fd/selinfo.c`**

```c
#include "../../../include/sys/selinfo.h"
#include "../../../include/error.h"
#include "../../core/task.h"
#include "../../../sched/scheduler.h"
#include "../../../sched/waitq.h"
#include "../../../lib/heap.h"
#include <string.h>

void selinfo_init(selinfo_t* si)
{
    if (!si) {
        return;
    }
    TAILQ_INIT(&si->waiters);
    spinlock_init(&si->lock);
}

int poll_begin(poll_ctx_t* pc, uint32_t nfds)
{
    if (!pc || nfds > POLL_MAX_REGS) {
        return RDNX_E_INVALID;
    }
    memset(pc, 0, sizeof(*pc));
    if (nfds <= POLL_INLINE_REGS) {
        pc->regs = pc->inline_regs;
        pc->cap  = POLL_INLINE_REGS;
        return RDNX_OK;
    }
    /* Аллокация происходит до засыпания: нехватка памяти возвращается
     * как ошибка входа в poll, а не бьёт по уже спящему потоку. */
    pc->regs = (sel_waiter_t*)kmalloc(nfds * sizeof(sel_waiter_t));
    if (!pc->regs) {
        return RDNX_E_NOMEM;
    }
    pc->cap = nfds;
    return RDNX_OK;
}

void sel_record(selinfo_t* si, poll_ctx_t* pc)
{
    if (!si || !pc || pc->n >= pc->cap) {
        return;
    }
    sel_waiter_t* w = &pc->regs[pc->n];
    w->thread = thread_get_current();
    w->pc     = pc;
    w->si     = si;

    irql_t old = set_irql(IRQL_HIGH);
    spinlock_lock(&si->lock);
    TAILQ_INSERT_TAIL(&si->waiters, w, si_link);
    spinlock_unlock(&si->lock);
    (void)set_irql(old);

    pc->n++;
}

void sel_wakeup(selinfo_t* si)
{
    if (!si) {
        return;
    }
    /* Вызывается в том числе из обработчика прерывания: спинлок берётся
     * без понижения IRQL, разбор списка не аллоцирует. */
    spinlock_lock(&si->lock);
    sel_waiter_t* w = NULL;
    TAILQ_FOREACH(w, &si->waiters, si_link) {
        if (w->pc) {
            w->pc->woken = 1;
        }
        if (w->thread) {
            scheduler_wake(w->thread);
        }
    }
    spinlock_unlock(&si->lock);
}

int poll_sleep(poll_ctx_t* pc, uint64_t deadline_ticks)
{
    if (!pc) {
        return RDNX_E_INVALID;
    }
    /* Проверка флага при поднятом IRQL закрывает окно между сканом
     * готовности и засыпанием: sel_wakeup выставляет woken под спинлоком. */
    irql_t old = set_irql(IRQL_HIGH);
    if (pc->woken) {
        pc->woken = 0;
        (void)set_irql(old);
        return RDNX_OK;
    }
    thread_t* self = thread_get_current();
    (void)set_irql(old);

    int ret = waitq_wait_until(&self->poll_wq, deadline_ticks);
    pc->woken = 0;
    return (ret == RDNX_E_TIMEOUT) ? RDNX_E_TIMEOUT : RDNX_OK;
}

void poll_end(poll_ctx_t* pc)
{
    if (!pc) {
        return;
    }
    for (uint32_t i = 0; i < pc->n; i++) {
        sel_waiter_t* w = &pc->regs[i];
        if (!w->si) {
            continue;
        }
        irql_t old = set_irql(IRQL_HIGH);
        spinlock_lock(&w->si->lock);
        TAILQ_REMOVE(&w->si->waiters, w, si_link);
        spinlock_unlock(&w->si->lock);
        (void)set_irql(old);
        w->si = NULL;
    }
    if (pc->regs && pc->regs != pc->inline_regs) {
        kfree(pc->regs);
    }
    pc->regs = NULL;
    pc->n = 0;
}
```

Добавить в `thread_t` (`kernel/core/task.h`) поле собственной очереди ожидания и инициализировать его в `thread_create` (`kernel/task.c`):

```c
    waitq_t poll_wq;   /* собственная очередь потока для ожидания готовности */
```

```c
    waitq_init(&thread->poll_wq, "poll");
```

- [ ] **Step 5: Убрать дубликат констант готовности**

`UNIX_POLLIN`/`UNIX_POLLOUT`/`UNIX_POLLERR`/`UNIX_POLLHUP`/`UNIX_POLLNVAL`
объявлены в `include/sys/file.h` (Task 1). Удалить безымянный enum из
`kernel/unix/fd/unix_fd.c:51-57` и убедиться, что файл берёт их из заголовка:

```bash
grep -n "UNIX_POLLIN" kernel/unix/fd/unix_fd.c include/sys/file.h
```

Expected: определение ровно одно, в `include/sys/file.h`.

- [ ] **Step 6: Реализовать `poll` в трёх ops**

В `kernel/unix/fd/pipe.c` — добавить `selinfo_t sel;` в `unix_pipe_t`, вызвать `selinfo_init(&p->sel)` при создании, вызывать `sel_wakeup(&p->sel)` в конце `pipe_fop_write` (появились данные), в `pipe_fop_read` (освободилось место) и в `pipe_fop_close` (изменилось число концов). Заменить `scheduler_yield()` в теле чтения на ожидание через `poll_ctx` с одной регистрацией.

```c
static short pipe_fop_poll(rdnx_file_t* f, short events, poll_ctx_t* pc)
{
    unix_pipe_t* p = (unix_pipe_t*)f->priv;
    if (!p || p->magic != UNIX_PIPE_MAGIC) {
        return UNIX_POLLNVAL;
    }
    if (pc) {
        sel_record(&p->sel, pc);
    }

    short rev = 0;
    irql_t old = unix_pipe_lock();
    uint32_t count = p->count, readers = p->readers, writers = p->writers;
    unix_pipe_unlock(old);

    if (f->status_flags & PIPE_END_READ) {
        if ((events & UNIX_POLLIN) && (count > 0 || writers == 0)) {
            rev |= UNIX_POLLIN;
        }
        if (writers == 0) {
            rev |= UNIX_POLLHUP;
        }
    } else {
        if ((events & UNIX_POLLOUT) && readers > 0 && count < UNIX_PIPE_CAP) {
            rev |= UNIX_POLLOUT;
        }
        if (readers == 0) {
            rev |= UNIX_POLLHUP | UNIX_POLLERR;
        }
    }
    return rev;
}
```

В `net/socket.c` — добавить `selinfo_t sel;` в `net_socket_t`, `selinfo_init` в `net_socket_create`, `sel_wakeup(&sock->sel)` в местах, где сокет становится готов: постановка UDP-датаграммы в очередь, дописывание в `tcp_conn.rx_buf`, постановка соединения в accept-очередь, смена состояния на `TCP_ST_ESTABLISHED` и на закрытие. Найти эти точки:

```bash
grep -n "rx_len\|accept_tail\|state = TCP_ST" net/socket.c
```

В `net/socket_file.c` — `sock_fop_poll` регистрируется на `&s->sel` и возвращает `UNIX_POLLIN` при наличии данных или готового соединения в accept-очереди, `UNIX_POLLOUT` для соединённого сокета. Для доступа к состоянию добавить в `net/socket.h` предикаты `int net_socket_readable(net_socket_t*);` и `int net_socket_writable(net_socket_t*);` с реализацией в `net/socket.c` — иначе `socket_file.c` придётся знать внутренности `net_socket_t`.

В `fs/vfs_file.c` — `vfs_fop_poll` для консольной ноды опрашивает `tty_console_poll_readable()` (перенести из `unix_poll_one`, `kernel/unix/fd/unix_fd.c:1433`), для обычного файла возвращает `UNIX_POLLIN | UNIX_POLLOUT` по маске `events`. Регистрация на `selinfo` консоли — из `console/tty_console.c`, где приходят символы; если там нет подходящего места, оставить консоль на опросе без регистрации и отметить это комментарием: обычный файл и консоль всегда готовы, поэтому `poll` по ним не засыпает.

Вписать `.poll` во все три таблицы.

- [ ] **Step 7: Переписать `unix_fs_poll`**

```c
uint64_t unix_fs_poll(uint64_t user_fds_ptr, uint64_t nfds, int64_t timeout_ms)
{
    proc_t* proc = proc_current();
    if (!proc || nfds > POLL_MAX_REGS) {
        return (uint64_t)RDNX_E_INVALID;
    }

    unix_pollfd_u_t kfds[POLL_INLINE_REGS];
    unix_pollfd_u_t* fds = kfds;
    if (nfds > POLL_INLINE_REGS) {
        fds = (unix_pollfd_u_t*)kmalloc((size_t)nfds * sizeof(*fds));
        if (!fds) {
            return (uint64_t)RDNX_E_NOMEM;
        }
    }
    if (nfds > 0 &&
        unix_copy_from_user(fds, (const void*)(uintptr_t)user_fds_ptr,
                            (size_t)nfds * sizeof(*fds)) != RDNX_OK) {
        if (fds != kfds) { kfree(fds); }
        return (uint64_t)RDNX_E_INVALID;
    }

    poll_ctx_t pc;
    int rc = poll_begin(&pc, (uint32_t)nfds);
    if (rc != RDNX_OK) {
        if (fds != kfds) { kfree(fds); }
        return (uint64_t)rc;
    }

    uint64_t deadline = 0;
    if (timeout_ms > 0) {
        uint64_t ticks = ((uint64_t)timeout_ms + (SCHEDULER_TIME_SLICE_MS - 1u)) /
                         SCHEDULER_TIME_SLICE_MS;
        deadline = scheduler_get_ticks() + ticks;
    }

    int ready = 0;
    for (;;) {
        ready = 0;
        for (uint64_t i = 0; i < nfds; i++) {
            rdnx_file_t* f = fd_get(proc, fds[i].fd);
            if (!f) {
                fds[i].revents = UNIX_POLLNVAL;
                ready++;
                continue;
            }
            short rev = f->ops->poll ? f->ops->poll(f, fds[i].events, &pc) : 0;
            fd_put(f);
            fds[i].revents = rev;
            if (rev != 0) {
                ready++;
            }
        }
        if (ready > 0 || timeout_ms == 0) {
            break;
        }
        if (timeout_ms > 0 && scheduler_get_ticks() >= deadline) {
            break;
        }
        if (poll_sleep(&pc, deadline) == RDNX_E_TIMEOUT) {
            break;
        }
        /* Регистрации снимаются и ставятся заново на каждом обороте:
         * набор дескрипторов мог измениться между пробуждениями. */
        poll_end(&pc);
        if (poll_begin(&pc, (uint32_t)nfds) != RDNX_OK) {
            break;
        }
    }

    poll_end(&pc);
    if (nfds > 0) {
        (void)unix_copy_to_user((void*)(uintptr_t)user_fds_ptr, fds,
                                (size_t)nfds * sizeof(*fds));
    }
    if (fds != kfds) {
        kfree(fds);
    }
    return (uint64_t)ready;
}
```

`unix_fs_select` переписать поверх той же схемы: разобрать три `fd_set` в массив `unix_pollfd_u_t`, вызвать общий внутренний хелпер, собрать результат обратно. Удалить `unix_poll_one` целиком.

- [ ] **Step 8: Зарегистрировать `selinfo.c` в сборке**

В `kernel/Makefile`:

```make
	kernel/unix/fd/selinfo.c \
```

- [ ] **Step 9: Собрать и прогнать**

Run: `make clean && make && TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: PASS, включая `CT-039`. Отдельно убедиться, что `polltest` и `selecttest` из `userland/bin` не сломались.

- [ ] **Step 10: Коммит**

```bash
git add include/sys/selinfo.h kernel/unix/fd/selinfo.c kernel/Makefile \
        kernel/core/task.h kernel/task.c kernel/unix/fd/pipe.c \
        net/socket.c net/socket.h net/socket_file.c fs/vfs_file.c \
        kernel/unix/fd/unix_fd.c userland/init/init.c
git commit -m "feat(fd): replace poll/select busy-wait with selinfo readiness

Pollable objects register waiters and wake them when they become ready, so
a poll with a timeout now sleeps. sel_wakeup is callable from interrupt
context, which is what lets a received packet wake accept and recv. The
pollfd array is also copied in and out instead of being dereferenced in
place."
```

---

## Task 6: Динамическая таблица дескрипторов

**Files:**
- Modify: `kernel/unix/proc.h` (`fd_table_t` в `proc_t`, удаление плоских массивов)
- Modify: `kernel/unix/process/unix_proc.c` (`proc_fd_alloc/get/close`, `proc_attach`/`proc_detach`)
- Modify: `kernel/unix/fd/unix_fd.c` (все обращения `fd >= PROC_MAX_FD`)
- Modify: `kernel/unix/process/unix_process.c`, `kernel/unix/exec/unix_exec.c` (обходы таблицы)
- Test: `userland/init/init.c`

**Interfaces:**
- Consumes: `rdnx_file_t`, `fd_install`/`fd_get`/`fd_close` (Task 1).
- Produces: `int proc_fd_grow(proc_t* proc, uint32_t want);`, `uint32_t proc_fd_size(const proc_t* proc);`

- [ ] **Step 1: Написать падающую контрактную проверку CT-040 (больше 32 дескрипторов)**

```c
    {
        /* CT-040: таблица дескрипторов растёт за пределы стартовых 32. */
        int local_ok = 1;
        int fds[64];
        int opened = 0;
        for (int i = 0; i < 64; i++) {
            long fd = posix_open("/etc/hostname", VFS_OPEN_READ);
            if (fd < 0) {
                break;
            }
            fds[opened++] = (int)fd;
        }
        if (opened < 40) {
            local_ok = 0;
        }
        for (int i = 0; i < opened; i++) {
            (void)posix_close(fds[i]);
        }
        if (local_ok) {
            ct_log("CT-040", "PASS", "fd table grows past 32");
        } else {
            ct_log("CT-040", "FAIL", "fd table capped at 32");
            ok = 0;
        }
    }
```

- [ ] **Step 2: Прогнать и убедиться, что проверка падает**

Run: `TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: `CT-040` FAIL — открывается не более 32 минус уже занятые stdin/stdout/stderr.

- [ ] **Step 3: Ввести `fd_table_t`**

В `kernel/unix/proc.h` заменить три поля на одно и объявить константы:

```c
#define PROC_FD_INITIAL  32u
#define PROC_FD_MAX      1024u

typedef struct fd_table {
    struct rdnx_file** files;
    uint8_t*           flags;   /* FD_CLOEXEC */
    uint32_t           size;
    spinlock_t         lock;
} fd_table_t;
```

В `proc_t` вместо `fd_table`/`fd_flags`/`fd_kind`:

```c
    fd_table_t fds;
```

`PROC_MAX_FD` сохранить как алиас `PROC_FD_MAX`, чтобы не править разом все проверки границ; вычистить алиас можно в Task 7.

- [ ] **Step 4: Реализовать рост таблицы**

В `kernel/unix/process/unix_proc.c`:

```c
int proc_fd_grow(proc_t* proc, uint32_t want)
{
    if (!proc || want > PROC_FD_MAX) {
        return RDNX_E_INVALID;
    }
    if (want <= proc->fds.size) {
        return RDNX_OK;
    }
    uint32_t ns = proc->fds.size ? proc->fds.size : PROC_FD_INITIAL;
    while (ns < want) {
        ns *= 2u;
    }
    if (ns > PROC_FD_MAX) {
        ns = PROC_FD_MAX;
    }

    struct rdnx_file** nf = (struct rdnx_file**)kmalloc(ns * sizeof(*nf));
    uint8_t* ng = (uint8_t*)kmalloc(ns);
    if (!nf || !ng) {
        if (nf) { kfree(nf); }
        if (ng) { kfree(ng); }
        return RDNX_E_NOMEM;
    }
    memset(nf, 0, ns * sizeof(*nf));
    memset(ng, 0, ns);
    if (proc->fds.files) {
        memcpy(nf, proc->fds.files, proc->fds.size * sizeof(*nf));
        memcpy(ng, proc->fds.flags, proc->fds.size);
        kfree(proc->fds.files);
        kfree(proc->fds.flags);
    }
    proc->fds.files = nf;
    proc->fds.flags = ng;
    proc->fds.size  = ns;
    return RDNX_OK;
}

uint32_t proc_fd_size(const proc_t* proc)
{
    return proc ? proc->fds.size : 0u;
}
```

`proc_fd_alloc` сканирует до `fds.size`, при исчерпании вызывает `proc_fd_grow(proc, fds.size + 1)` и повторяет; при `RDNX_E_NOMEM` или достижении `PROC_FD_MAX` возвращает `RDNX_E_BUSY`. Начальная таблица заводится в `proc_attach()` вызовом `proc_fd_grow(proc, PROC_FD_INITIAL)`; в `proc_detach()` `files` и `flags` освобождаются после закрытия всех дескрипторов.

- [ ] **Step 5: Обновить обходы таблицы**

```bash
grep -rn "PROC_MAX_FD\|fd_table\[\|fd_flags\[" --exclude-dir=.git kernel/ fs/ sched/ shell/
```

Каждый цикл `for (int fd = 0; fd < PROC_MAX_FD; fd++)` заменить на `for (uint32_t fd = 0; fd < proc_fd_size(proc); fd++)`; каждую проверку границы `fd >= PROC_MAX_FD` — на `fd >= (int)proc_fd_size(proc)`. Обращения `proc->fd_table[fd]` заменить на `proc->fds.files[fd]`, `proc->fd_flags[fd]` — на `proc->fds.flags[fd]`. Там, где в руках только `task_t*`, персоналия берётся как `proc_t* proc = task_proc(task);`.

В `unix_clone_fds_for_spawn` перед копированием вызвать `proc_fd_grow(cproc, proc_fd_size(pproc))`.

- [ ] **Step 6: Снять лимит `nfds` в `poll`, зафиксировать лимит `select`**

В `unix_fs_poll` проверка уже опирается на `POLL_MAX_REGS` (Task 5). Проверить, что нигде не осталось `nfds > PROC_MAX_FD`:

```bash
grep -n "nfds" kernel/unix/fd/unix_fd.c
```

**`select` остаётся ограничен 32 дескрипторами, и это не упущение.** Его
`fd_set` — это `typedef struct unix_fdset_u { uint32_t bits; }`
(`kernel/unix/fd/unix_fd.c:75`), одно 32-битное слово: дескриптор со
значением 32 и выше в нём непредставим. Расширение — это смена
пользовательского ABI, она в область этого плана не входит.

Поэтому в `unix_fs_select` оставить явную проверку и внятный отказ:

```c
    if (nfds > 32) {
        /* fd_set — одно 32-битное слово; расширение требует смены ABI.
         * poll() ограничений не имеет и является предпочтительным. */
        return (uint64_t)RDNX_E_INVALID;
    }
```

- [ ] **Step 7: Собрать и прогнать**

Run: `make clean && make && TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: PASS, включая `CT-040`.

- [ ] **Step 8: Коммит**

```bash
git add kernel/unix/proc.h kernel/unix/process/unix_proc.c \
        kernel/unix/fd/unix_fd.c \
        kernel/unix/process/unix_process.c kernel/unix/exec/unix_exec.c \
        userland/init/init.c
git commit -m "feat(fd): grow the descriptor table on demand

The table starts at 32 entries and doubles up to 1024 instead of living as
a fixed array inside task_t, which also takes about 320 bytes off every
task."
```

---

## Task 7: Удаление тега типа

Завершающая задача: из дерева исчезает всякое различение типов дескрипторов вне ops.

**Files:**
- Modify: `include/sys/file.h` (удаление поля `kind`)
- Modify: `kernel/unix/unix_layer.h` (удаление `UNIX_FD_KIND_*`)
- Modify: `kernel/unix/fd/unix_fd.c`
- Modify: `kernel/linux/linux_compat.c` (7 сайтов)
- Modify: `kernel/posix/posix_sys_vm.c:76`

**Interfaces:**
- Consumes: `socket_file_sock` (Task 3), `f->ops` как признак типа.
- Produces: ничего нового; убирает `UNIX_FD_KIND_*` из публичных заголовков.

- [ ] **Step 1: Найти все оставшиеся обращения**

```bash
grep -rn "UNIX_FD_KIND_\|->kind" --exclude-dir=.git --exclude-dir=docs .
```

Ожидается: 7 сайтов в `kernel/linux/linux_compat.c`, 1 в `kernel/posix/posix_sys_vm.c`, объявления в `kernel/unix/unix_layer.h`, поле в `include/sys/file.h` и его выставление в `unix_fd.c`.

- [ ] **Step 2: Заменить проверки типа на сравнение ops**

В `kernel/posix/posix_sys_vm.c:76` проверка «дескриптор ссылается на файл» становится (`proc` берётся как `task_proc(task)` — задача там уже в руках):

```c
    rdnx_file_t* f = fd_get(proc, fd);
    if (!f || f->ops != &vfs_fileops) {
        if (f) { fd_put(f); }
        return (uint64_t)RDNX_E_INVALID;
    }
```

В `kernel/linux/linux_compat.c` каждая из семи проверок заменяется по тому же образцу: сравнение с `&vfs_fileops` или вызов `socket_file_sock(f)` там, где нужен сокет. Объявить `extern const file_ops_t vfs_fileops;` в `fs/vfs.h`.

- [ ] **Step 3: Удалить поле и перечисление**

Убрать `uint8_t kind;` из `struct rdnx_file` и все его присваивания; убрать перечисление `UNIX_FD_KIND_*` из `kernel/unix/unix_layer.h:24-25` и далее.

- [ ] **Step 4: Проверить критерии готовности**

```bash
grep -rn "UNIX_FD_KIND_" --exclude-dir=.git --exclude-dir=docs .
grep -rn "fd_kind" --exclude-dir=.git --exclude-dir=docs .
```

Expected: пусто в обоих случаях.

```bash
grep -n "kind\|UNIX_FD_KIND" kernel/unix/fd/unix_fd.c
```

Expected: пусто. В файле не остаётся ни одной ветки, различающей тип дескриптора.

- [ ] **Step 5: Собрать и прогнать**

Run: `make clean && make && TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: PASS, весь набор `CT-001`…`CT-040`.

- [ ] **Step 6: Коммит**

```bash
git add include/sys/file.h kernel/unix/unix_layer.h kernel/unix/fd/unix_fd.c \
        kernel/linux/linux_compat.c kernel/posix/posix_sys_vm.c fs/vfs.h
git commit -m "refactor(fd): drop the descriptor kind tag

Type is now identified by the ops pointer where a syscall genuinely needs a
specific kind, so nothing outside a subsystem enumerates descriptor types."
```

---

## Task 8: Регрессионный гейт в CI

**Files:**
- Modify: `.github/workflows/smoke.yml`

**Interfaces:**
- Consumes: `scripts/ci/contract_qemu.sh`.
- Produces: прогон контрактного набора на каждый push и pull request.

- [ ] **Step 1: Добавить триггеры и шаг прогона**

В `.github/workflows/smoke.yml` заменить блок `on:`:

```yaml
on:
  workflow_dispatch:
  push:
    branches: [ main ]
  pull_request:
```

И после существующего шага `Run Smoke` добавить:

```yaml
      - name: Run Contract Suite
        run: |
          TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh
```

- [ ] **Step 2: Проверить прогон локально теми же командами, что и в CI**

Run: `TIMEOUT_SEC=30 scripts/ci/smoke_qemu.sh && TIMEOUT_SEC=60 scripts/ci/contract_qemu.sh`
Expected: оба скрипта завершаются с кодом 0.

- [ ] **Step 3: Коммит**

```bash
git add .github/workflows/smoke.yml
git commit -m "ci: run the contract suite on push and pull request

The workflow only ran the boot smoke test, and only on manual dispatch, so
descriptor-layer regressions could not be caught before merge."
```

---

## Вне области

Не входит в этот план, зафиксировано в спеке, раздел 6:

- `epoll` / `eventfd` / `timerfd`
- SMP и модель блокировок
- Расширение набора кодов ошибок в `include/error.h`
- Перевод `read`/`write` на copyin/copyout пользовательских буферов (сейчас буфер передаётся в ops как пользовательский указатель после `unix_user_range_ok`; в Task 5 на copyin переводится только массив `pollfd`)
- Второй слой: замена `if (inode->flags & VFS_INODE_*)` в `fs/vfs.c` на ops в иноде — следующая спека
