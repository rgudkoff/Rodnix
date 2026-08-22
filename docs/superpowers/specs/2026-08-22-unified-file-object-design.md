# Единый объект файла: `rdnx_file_t` + `file_ops_t`

Дата: 2026-08-22
Статус: дизайн согласован, план реализации не написан

## 1. Задача

Сделать ядро расширяемым в следующем смысле: **добавление новой подсистемы,
предоставляющей файловые дескрипторы, не должно требовать правок в чужих
файлах.**

Ближайший потребитель — сетевой стек. Полноценный TCP приносит новые типы
дескрипторов (сокет, слушающий сокет, позже — механизм уведомлений). Пока
диспетчеризация по типу дескриптора устроена как `switch`, каждый такой тип
множит ветки в коде, который к сети отношения не имеет.

Документ покрывает **первый слой** — дескрипторы. Второй слой (диспетчеризация
внутри VFS по `VFS_INODE_*`) вынесен в отдельную спеку.

## 2. Текущее состояние

### 2.1 Слой дескрипторов

`task_t` хранит три плоских массива (`kernel/core/task.h:135`):

```c
void*   fd_table[TASK_MAX_FD];  /* TASK_MAX_FD == 32 */
uint8_t fd_flags[TASK_MAX_FD];
uint8_t fd_kind[TASK_MAX_FD];
```

Тип объекта хранится отдельно от указателя на него. Диспетчеризация — цепочки
`if (kind == UNIX_FD_KIND_VFS) ... PIPE_R ... PIPE_W ... SOCKET`.

Распределение сайтов проверки `UNIX_FD_KIND_*`:

| файл | сайтов |
|---|---|
| `kernel/unix/fd/unix_fd.c` | 51 |
| `kernel/linux/linux_compat.c` | 7 |
| `kernel/unix/unix_layer.h` | 5 |
| `kernel/posix/posix_sys_vm.c` | 1 |

Итого 64, из них 51 — в одном файле. Радиус поражения ограничен.

### 2.2 Следствия, наблюдаемые сегодня

- **`dup` копирует структуру, а не разделяет описание.** `unix_fd_dup_into`
  (`kernel/unix/fd/unix_fd.c:376`) делает `kmalloc` + `vfs_file_dup`. Offset у
  двух fd независим — это нарушение POSIX. Ломается `>>` и разделение позиции
  между процессами.
- **`dup(socket)` возвращает `RDNX_E_UNSUPPORTED`** (`unix_fd.c:413`).
- **Сокеты не poll-абельны.** `unix_poll_one` (`unix_fd.c:1483`) возвращает на
  них `POLLNVAL`.
- **`poll`/`select` — busy-wait.** Цикл «опросить все fd → `scheduler_yield()`»
  без какого-либо механизма уведомления. Разрешение таймаута равно кванту
  планировщика.
- **Потолок 32 дескриптора**, он же потолок `nfds` в `poll`.
- **Счётчики `readers`/`writers` в `unix_pipe_t`** — это refcount,
  переизобретённый для одного конкретного типа.

`waitq` (`sched/waitq.h`) в дереве уже есть и полностью реализован, но
используется только планировщиком и shell. Слой дескрипторов его не знает.

## 3. Решение

### 3.1 Разделение описания и дескриптора

Вводится различие, которого в дереве сейчас нет:

- **Описание открытого файла** (`rdnx_file_t`) — владеет `pos`, `O_APPEND`,
  `O_NONBLOCK`. Разделяется между всеми `dup` и между родителем и потомком
  после `fork`.
- **Дескриптор** (номер в таблице задачи) — владеет `FD_CLOEXEC`.

```c
/* include/sys/file.h */
#include <sys/selinfo.h>   /* poll_ctx_t */

typedef struct rdnx_file rdnx_file_t;

typedef struct file_ops {
    const char* name;                 /* "vfs" / "pipe" / "socket" */
    int64_t (*read)  (rdnx_file_t*, void* kbuf, size_t len);
    int64_t (*write) (rdnx_file_t*, const void* kbuf, size_t len);
    int64_t (*seek)  (rdnx_file_t*, int64_t off, int whence);
    int     (*ioctl) (rdnx_file_t*, uint64_t req, void* karg);
    int     (*stat)  (rdnx_file_t*, vfs_stat_t* out);
    short   (*poll)  (rdnx_file_t*, short events, poll_ctx_t* pc);
    int     (*close) (rdnx_file_t*); /* только на последней ссылке */
} file_ops_t;

struct rdnx_file {
    const file_ops_t* ops;
    uint32_t  refs;
    uint32_t  status_flags;  /* O_APPEND / O_NONBLOCK */
    int64_t   pos;
    void*     priv;          /* vfs_node_t* / unix_pipe_t* / net_socket_t* */
};
```

Буферы в `read`/`write`/`ioctl`/`stat` — **ядровые**. Копирование из/в
пользовательскую память происходит в syscall-обвязке, до вызова ops. Реализации
ops никогда не видят пользовательских указателей.

`ssize_t` в дереве отсутствует, поэтому возврат — `int64_t`, отрицательное
значение есть `RDNX_E_*`. Это согласуется с текущими `vfs_read`/`vfs_write`,
которые возвращают `int`.

Отсутствующий указатель в `file_ops_t` означает «операция не поддерживается» и
даёт `RDNX_E_UNSUPPORTED`; проверка на `NULL` живёт в обвязке, а не в каждой
реализации.

`fd_kind` удаляется из `task_t` полностью.

### 3.2 Владение кодом

| файл | содержимое | статус |
|---|---|---|
| `include/sys/file.h` | `rdnx_file_t`, `file_ops_t` | новый |
| `kernel/unix/fd/file.c` | объект, refcount, таблица дескрипторов | новый |
| `fs/vfs_file.c` | `vfs_fileops` поверх существующих `vfs_read`/`vfs_write` | новый |
| `kernel/unix/fd/pipe.c` | `pipe_fileops` + `unix_pipe_t` | вынос из `unix_fd.c` |
| `net/socket_file.c` | `socket_fileops` | новый, принадлежит сети |
| `kernel/unix/fd/unix_fd.c` | только syscall-обвязка | сокращается |

Каждая ops-таблица живёт у своей подсистемы. Слой дескрипторов не знает ни
одного конкретного типа. Новая подсистема добавляет тип, отдавая
`rdnx_file_t*` с собственной ops-таблицей, и не правит ничего в `kernel/unix/fd/`.

### 3.3 Жизненный цикл

```
rdnx_file_alloc(ops, priv) -> refs = 1
fd_install(task, f)        -> занять слот в таблице
fd_get(task, fd)           -> +1, вернуть указатель
fd_put(f)                  -> -1; при 0 -> ops->close() + kfree
```

`fd_get`/`fd_put` оборачивают каждую операцию. Это закрывает гонку «закрытие
дескриптора во время блокирующей операции над ним» — для сети это штатная
ситуация, для текущего кода она пока не возникает.

Переходы:

- `dup` / `dup2` / `dup3` — `refs++`, новый дескриптор, `FD_CLOEXEC`
  сбрасывается. Копирования структуры больше нет.
- `fork` — `refs++` на каждый наследуемый дескриптор.
- `close` / cloexec при `exec` / `exit` — `fd_put`.

Побочно исправляется: разделяемый offset, `dup(socket)`, наследование сокетов
через `fork`.

`unix_pipe_t` сохраняет собственные счётчики `readers`/`writers` — от них
зависит `POLLHUP`, а он различает концы пайпа, чего общий refcount не делает.
Но это становится внутренним делом `pipe_fileops`.

### 3.4 Согласование с `vfs_node.ref_count`

Сейчас `vfs_open` инкрементирует `vfs_node.ref_count` (`fs/vfs.h:56`), а
`unix_fd_release` вызывает `vfs_close` **на каждый дескриптор**. После введения
refcount на описании `vfs_close` будет вызван один раз на описание.

Целевой инвариант:

> `vfs_node.ref_count` равен числу живых `rdnx_file_t`, ссылающихся на ноду,
> плюс единица за ссылку из дерева имён, пока `unlinked == false`.

Это самое опасное место миграции: при неверном сведении ноды либо потекут, либо
будут освобождены под живым дескриптором. Тест на это в дереве отсутствует и
должен быть написан **до** изменения.

### 3.5 Модель готовности

Ограничение планировщика: у потока одно поле `waitq_owner` (`sched/waitq.c`),
то есть поток может спать ровно на одной очереди. `poll` по определению ждёт на
N объектах. Схема «по `waitq` на объект, уснуть на всех» невозможна без
переделки планировщика.

Применяется схема BSD `selinfo`: поток спит на своей единственной очереди,
объект будит зарегистрированных на нём ожидающих.

```c
/* include/sys/selinfo.h */
typedef struct sel_waiter {
    thread_t*  thread;
    struct selinfo* si;
    TAILQ_ENTRY(sel_waiter) si_link;
} sel_waiter_t;

typedef struct selinfo {          /* встраивается в разделяемый объект */
    TAILQ_HEAD(, sel_waiter) waiters;
    spinlock_t lock;
} selinfo_t;

#define POLL_MAX_REGS    256   /* потолок nfds */
#define POLL_INLINE_REGS 8     /* типичный случай — без аллокации */

typedef struct poll_ctx {
    sel_waiter_t  inline_regs[POLL_INLINE_REGS];
    sel_waiter_t* regs;        /* == inline_regs при n <= POLL_INLINE_REGS */
    uint32_t      n;
    volatile int  woken;
} poll_ctx_t;

void sel_record(selinfo_t* si, poll_ctx_t* pc);
void sel_wakeup(selinfo_t* si);
```

`spinlock_t` берётся из `kernel/fabric/spin.h`. Формально заголовок лежит внутри
Fabric, фактически это уже общая примитива ядра — его включают `net/`, `fs/`,
`kernel/ipc.c`, `kernel/unix/process/`. Переезд заголовка на нейтральный путь в
эту спеку не входит.

`selinfo` размещается **не** в `rdnx_file_t`, а в разделяемом объекте под ним:
в `unix_pipe_t`, `net_socket_t`, в иноде tty. Причина: у пайпа read-конец и
write-конец — разные описания при одном `unix_pipe_t`; писатель должен разбудить
читателя, чьё описание ему недоступно. Готовность — свойство объекта, не
описания.

Цикл ожидания:

```c
poll_begin(&pc);
for (;;) {
    ready = 0;
    for (i < nfds)
        ready += f[i]->ops->poll(f[i], events[i], &pc);  /* ops зовёт sel_record */
    if (ready || timeout == 0) break;
    if (poll_sleep(&pc, deadline) == TIMED_OUT) break;
}
poll_end(&pc);   /* снять все регистрации */
```

Регистрация происходит **до** проверки готовности. `sel_wakeup` под спинлоком
выставляет `pc->woken`; `poll_sleep` проверяет флаг при поднятом IRQL перед тем,
как уснуть. Этим закрывается потеря пробуждения между сканом и засыпанием.

Требование: **`sel_wakeup` должен быть вызываемым из контекста прерывания.** В
этом весь смысл изменения — приход пакета в RX-обработчике обязан будить
`accept`/`recv`. Клавиатурный IRQ получает то же самое.

Блокирующие `read`/`write` используют тот же механизм с одной регистрацией.
Отдельного пути для них нет.

Размещение регистраций. Ядровый стек потока — 32 KiB (`KERNEL_STACK_SIZE`,
`kernel/task.c:21`), а `sizeof(sel_waiter_t)` порядка 32 байт. Массив из 256
элементов целиком на стеке — это 8 KiB, четверть стека в одной локальной
переменной внутри syscall-пути, под которым уже лежат кадры. Поэтому: до
`POLL_INLINE_REGS` регистраций используется встроенный массив, свыше — одна
`kmalloc` на `n * sizeof(sel_waiter_t)`, освобождаемая в `poll_end`.

Аллокация происходит **до** засыпания, не в ожидании: отказ по памяти
возвращается как ошибка входа в `poll`, а не как отказ у уже спящего потока.

`POLL_MAX_REGS` остаётся потолком `nfds` и после перехода на кучу — он
ограничивает объём ядровой аллокации, управляемый пользовательским параметром.
Текущий потолок — 32, новый — 256.

Заодно устраняется отдельный дефект: сейчас `unix_fs_poll` приводит
пользовательский указатель к `unix_pollfd_u_t*` и разыменовывает его напрямую
(`pfds[i]`), проверив только `unix_user_range_ok`. Массив `pollfd` должен
копироваться в ядро на входе и выгружаться обратно на выходе. Это входит в шаг 5.

### 3.6 Таблица дескрипторов

```c
typedef struct fd_table {
    rdnx_file_t** files;
    uint8_t*      flags;   /* только FD_CLOEXEC */
    uint32_t      size;
    spinlock_t    lock;
} fd_table_t;
```

Начальная ёмкость 32, удвоение при исчерпании до потолка 1024. Семантика
«наименьший свободный дескриптор» сохраняется линейным сканом. `task_t` худеет
на ~320 байт: три inline-массива уезжают в кучу.

Снимается ограничение `nfds > TASK_MAX_FD` в `poll` и `select`.

## 4. Порядок работ

Инвариант: **дерево загружается и проходит гейт после каждого шага.** Не
допускается состояние «большой рефакторинг, один зелёный в конце».

| # | шаг | гейт |
|---|---|---|
| 1 | `include/sys/file.h`, `kernel/unix/fd/file.c`, `fs/vfs_file.c`. Переведён только VFS-путь; `fd_kind` временно жив для pipe и socket | boot -> `/bin/init` -> prompt; `contract_fd`, `contract_fsio`, `contract_fd_inherit` |
| 2 | Сведение `vfs_node.ref_count` (раздел 3.4) | `contract_unlink_open` (новый, пишется первым) |
| 3 | Пайп -> `kernel/unix/fd/pipe.c`, удаление `UNIX_FD_KIND_PIPE_R/W` | `pipetest`, пайплайны в shell |
| 4 | Сокет -> `net/socket_file.c`, удаление `UNIX_FD_KIND_SOCKET`, удаление `fd_kind` из `task_t` | `grep -rn UNIX_FD_KIND_` пуст; `ping`, `ifconfig`, tcpremote |
| 5 | `selinfo` / `poll_ctx`; `poll` и `select` через `ops->poll`, busy-wait удалён | `polltest`, `contract_poll_idle` |
| 6 | Динамическая таблица дескрипторов, снятие лимита `nfds` | `contract_fd` с 200 дескрипторами |
| 7 | `kernel/linux/linux_compat.c` (7 сайтов), `kernel/posix/posix_sys_vm.c:76` | пересборка, `syscalltest` |

Шаг 6 — единственный, который можно отложить без ущерба для цели. Включён,
потому что `task_fd_alloc`/`task_fd_get`/`task_fd_close` переписываются целиком
на шаге 1, а потолок 32 упрётся на первом сетевом сервере.

## 5. Проверка

Новые контрактные тесты в `userland/bin/`, в стиле существующих `contract_*.c`:

| тест | проверяет |
|---|---|
| `contract_fd_shared_offset` | `dup` + запись в оба дескриптора — offset общий |
| `contract_fd_socket_dup` | `dup(socket)` работает; закрытие одного не рвёт второй |
| `contract_fd_refcount` | `fork`, `close` в родителе — у потомка дескриптор жив |
| `contract_unlink_open` | чтение unlink'нутого файла до последнего `close`; освобождение ноды после |
| `contract_poll_idle` | `poll(timeout=1s)` не даёт роста CPU-времени задачи |

`contract_poll_idle` опирается на учёт CPU по `thread_group`, который уже есть и
виден через `ps`.

Регрессионный гейт: `scripts/ci/contract_qemu.sh` подключается в
`.github/workflows/smoke.yml`. Сейчас там запускается только `smoke_qemu.sh`, и
только по `workflow_dispatch` — регрессии на PR не ловятся.

## 6. Вне области

Сознательно не входит в эту спеку:

- `epoll` / `eventfd` / `timerfd` — новые типы дескрипторов поверх готовой
  абстракции, отдельный заход
- SMP и модель блокировок, per-CPU структуры
- Расширение пространства ошибок (`include/error.h`, сейчас 10 кодов) до набора,
  нужного сети
- **Второй слой**: замена `if (inode->flags & VFS_INODE_*)` в `fs/vfs.c` (13
  сайтов, 7 флагов типов) на ops в иноде и регистрацию `/dev`-нод через Fabric.
  Идёт следующей спекой сразу после этой.

## 7. Критерий готовности

1. `grep -rn "UNIX_FD_KIND_"` по дереву не даёт совпадений.
2. `fd_kind` отсутствует в `task_t`.
3. В `kernel/unix/fd/unix_fd.c` нет ни одной ветки, различающей тип
   дескриптора.
4. `poll` с непустым таймаутом не потребляет CPU в ожидании.
5. Все контрактные тесты из раздела 5 проходят в `contract_qemu.sh`.
