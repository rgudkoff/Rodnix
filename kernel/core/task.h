/**
 * @file task.h
 * @brief Архитектурно-независимый интерфейс для задач и потоков
 * 
 * Определяет абстракции для работы с задачами и потоками.
 */

#ifndef _RODNIX_CORE_TASK_H
#define _RODNIX_CORE_TASK_H

#include "arch_types.h"
#include "cpu.h"
#include "../fabric/spin.h"
#include <bsd/sys/queue.h>
#include <bsd/sys/tree.h>
#include <stdint.h>
#include <stdbool.h>

struct interrupt_frame;

/* UNIX-персоналия задачи (креды, дескрипторы, cwd, сигналы).
 * Полное определение — kernel/unix/proc.h; ядро видит только указатель. */
struct rdnx_proc;
typedef struct rdnx_proc proc_t;

/* ============================================================================
 * Состояние задачи
 * ============================================================================ */

typedef enum {
    TASK_STATE_NEW = 0,        /* Новая задача */
    TASK_STATE_READY,          /* Готова к выполнению */
    TASK_STATE_RUNNING,        /* Выполняется */
    TASK_STATE_BLOCKED,        /* Заблокирована */
    TASK_STATE_SLEEPING,       /* Спит */
    TASK_STATE_ZOMBIE,         /* Завершена, но не удалена */
    TASK_STATE_DEAD,           /* Удалена */
} task_state_t;

typedef enum {
    TASK_ABI_NATIVE = 0,
    TASK_ABI_LINUX = 1,
} task_abi_t;

/* ============================================================================
 * Состояние потока
 * ============================================================================ */

typedef enum {
    THREAD_STATE_NEW = 0,      /* Новый поток */
    THREAD_STATE_READY,        /* Готов к выполнению */
    THREAD_STATE_RUNNING,      /* Выполняется */
    THREAD_STATE_BLOCKED,      /* Заблокирован */
    THREAD_STATE_SLEEPING,     /* Спит */
    THREAD_STATE_DEAD,         /* Завершен */
} thread_state_t;

/*
 * Состояние потока для планировщика — набор битов, а не одно значение.
 *
 * Причина не в удобстве. Поток бывает одновременно «уже помечен ожидающим» и
 * «ещё исполняется» — именно в этот промежуток попадает пробуждение,
 * приходящее с другого процессора между проверкой условия и засыпанием. Одним
 * значением такое состояние невыразимо: любое присвоение теряет половину
 * истины, и пробуждение пропадает вместе с ней. Измерено: поток BLOCKED, ни в
 * одной очереди ожидания, разбудить нечем, вся система стоит.
 *
 * XNU держит то же самое теми же битами (osfmk/kern/thread.h). Его TH_WAKING
 * — «between waitq remove and thread_go» — у нас отсутствует намеренно: XNU
 * отпускает замок очереди между снятием потока и доставкой пробуждения и
 * вынужден охранять зазор битом; у нас это одна критическая секция
 * (waitq_wake_one), зазора нет, охранять нечего.
 *
 * Готовность и исполнение — один бит TH_RUN, как у XNU. Что поток именно
 * исполняется, а не стоит в очереди, знает процессор: percpu.current_thread.
 * Разделять их в самом состоянии значило бы снова заводить переход, который
 * можно потерять.
 */
#define TH_RUN     0x01u   /* исполняется или стоит в очереди готовности */
#define TH_WAIT    0x02u   /* ждёт события */
#define TH_IDLE    0x04u   /* поток простоя своего процессора */
#define TH_DEAD    0x08u   /* завершился, ждёт сборщика */
/* Поток дошёл до своей точки сна: scheduler_block() вызван. Без этого бита
 * арбитр обязан считать поток с TH_WAIT готовым и вернуть его в очередь.
 *
 * Различие содержательное. Между объявлением ожидания (waitq_enqueue) и
 * точкой сна поток ещё может держать спящие замки — Giant дают обратно
 * только в waitq_wait_until. Усыпить его в этом окне значит усыпить
 * держателя Giant: тот, кто должен его разбудить, в сисколл уже не войдёт.
 * FreeBSD в этом окне возвращает поток в run-queue (sleepq_add сделан,
 * sleepq_wait ещё нет — TDS_RUNNING), XNU просто отказывает преемпции
 * (ast_taken_kernel/waitq_wait_possible). Наш switch живёт в прерывании и
 * отказать не может — поэтому фиксация отдельным битом. */
#define TH_BLOCK   0x10u   /* спать можно: точка сна достигнута, замки отданы */

/* ============================================================================
 * Приоритет
 * ============================================================================ */

#define PRIORITY_MIN 0
#define PRIORITY_MAX 255
#define PRIORITY_DEFAULT 128

/* ============================================================================
 * Scheduling class
 * ============================================================================ */

enum {
    SCHED_CLASS_TIMESHARE = 0,
    SCHED_CLASS_REALTIME  = 1,
};

/* ============================================================================
 * QoS bucket (иерархический планировщик v1)
 * Значение = индекс в ready_queues[]; выше — приоритетнее.
 * ============================================================================ */

typedef enum {
    SCHED_BUCKET_BACKGROUND  = 0, /* фоновые задачи, batch */
    SCHED_BUCKET_UTILITY     = 1, /* фоновые сервисы */
    SCHED_BUCKET_DEFAULT     = 2, /* обычные процессы */
    SCHED_BUCKET_INTERACTIVE = 3, /* интерактивные, латентно-чувствительные */
    SCHED_BUCKET_COUNT       = 4,
} sched_bucket_t;

/* ============================================================================
 * Группа потоков (CPU-учёт на уровне задачи)
 * Встраивается в task_t; все потоки задачи разделяют одну группу.
 * ============================================================================ */

typedef struct {
    uint64_t cpu_ticks;      /* суммарные тики CPU всех потоков группы */
    uint64_t last_run_tick;  /* последний тик, когда группа получила CPU */
} thread_group_t;

/* ============================================================================
 * Задача (адресное пространство + ресурсы)
 * ============================================================================ */

/* ============================================================================
 * Embedded wait queue (defined here to break circular include with waitq.h)
 * ============================================================================ */

TAILQ_HEAD(waitq_thread_head, thread);

typedef struct waitq {
    struct waitq_thread_head threads;
    const char* name;
    uint32_t count;
} waitq_t;

typedef struct task {
    uint64_t task_id;          /* Уникальный ID задачи */
    uint64_t parent_task_id;   /* Родительская задача (0 для kernel/orphan) */
    /* Адресное пространство как непрозрачный pmap_t, а не физический адрес
     * корня таблиц: см. mm/pmap.h. Раньше здесь лежал void*, в котором на
     * самом деле хранился pml4_phys, и всякий, кому нужно было отобразить
     * страницу, приводил его обратно к числу и собирал запись сам. */
    struct pmap* address_space;
    void* vm_map;              /* VM map (unix-style user virtual memory map) */
    /* Раскладка пользовательского адресного пространства: владелец — mm/vm_map.c
     * (vm_task_brk/vm_task_mmap*). Осознанно оставлено в задаче, чтобы VM-слой
     * не зависел от POSIX-персоналии. */
    uint64_t vm_brk_base;      /* Base of brk() region (end of data/bss image) */
    uint64_t vm_brk_end;       /* Current program break */
    uint64_t vm_mmap_base;     /* Base for mmap() allocations */
    uint64_t vm_mmap_hint;     /* Next mmap() search hint */
    task_state_t state;        /* Состояние задачи */
    uint8_t abi;               /* task_abi_t: селектор персоналии (читается arch-кодом) */
    uint64_t tls_fs_base;      /* userspace FS base (arch_prctl/linux ABI) */
    proc_t* proc;              /* UNIX-персоналия (kernel/unix/proc.h): заводится в task_create(),
                                * читатели обязаны считать NULL допустимым */
    struct thread* main_thread;/* Основной поток процесса */
    TAILQ_HEAD(thread_list, thread) threads; /* Список всех потоков задачи */
    uint32_t thread_count;     /* Количество потоков задачи */
    uint32_t ref_count;        /* Счетчик ссылок */
    struct task* next_all;     /* Связный список всех задач */
    RB_ENTRY(task) task_id_link; /* Узел task_id-индекса */
    void* arch_specific;       /* Архитектурно-зависимые данные */
    thread_group_t thread_group; /* CPU-учёт группы для планировщика */
} task_t;

/* ============================================================================
 * Поток (thread)
 * ============================================================================ */

typedef struct thread {
    uint64_t thread_id;        /* Уникальный ID потока */
    task_t* task;              /* Задача, к которой принадлежит поток */
    thread_context_t context;   /* Контекст выполнения */
    /* Набор битов TH_*, меняется атомарно. Читать через предикаты ниже, а не
     * сравнением с одним значением — сравнение и есть та потеря, из-за которой
     * пробуждения пропадали. */
    volatile uint32_t state;
    uint8_t sched_class;       /* Класс планирования */
    uint8_t priority;          /* Приоритет потока */
    uint8_t base_priority;     /* Базовый приоритет */
    int16_t dyn_priority;      /* Динамический приоритет (с учётом boost/penalty) */
    int16_t inherited_priority;/* Приоритет по наследованию */
    uint8_t has_inherited;     /* Флаг наследования */
    int16_t inherit_stack[8];  /* Стек наследованных приоритетов */
    uint8_t inherit_depth;     /* Глубина стека наследования */
    uint8_t has_inherit_overflow; /* Флаг переполнения стека наследования */
    uint8_t sched_bucket;          /* sched_bucket_t: QoS-бакет (устанавливается до add_thread) */
    uint8_t sched_bucket_explicit; /* 1 — бакет задан явно; 0 — scheduler_add_thread назначит DEFAULT */
    uint32_t sched_usage;      /* Счётчик использования CPU */
    uint64_t last_sleep_tick;  /* Последний тик блокировки */
    void (*entry)(void*);      /* Точка входа потока */
    void* arg;                 /* Аргумент для точки входа */
    void* stack;               /* Указатель на стек */
    size_t stack_size;         /* Размер стека */
    TAILQ_ENTRY(thread) task_link;  /* Узел списка потоков задачи (task_t.threads) */
    TAILQ_ENTRY(thread) sched_link; /* Узел ready-очереди планировщика */
    uint8_t ready_queued;      /* Поток находится в ready queue */
    /* Nonzero while some processor is still executing on this thread's kernel
     * stack. Set when a CPU starts running the thread and cleared by the
     * interrupt stub only after it has switched off the stack, so a processor
     * that picks the thread out of the run queue knows when the stack is
     * genuinely free. Written by assembly through a pointer, so its type must
     * stay 32-bit. */
    volatile uint32_t on_cpu;
    TAILQ_ENTRY(thread) wait_link;  /* Узел waitq-очереди */
    TAILQ_ENTRY(thread) wait_timeout_link; /* Узел глобального timeout-list ожидания */
    struct waitq* waitq_owner;      /* Текущая waitq, если поток ожидает */
    /* Дедлайн ожидания в наносекундах ktime (0 = без дедлайна).
     *
     * В тиках он держаться не может: тики теряются, когда обработчик не
     * успевает за периодом — измерено 20% потерь на 1000 Гц, — и тогда сон
     * длится дольше запрошенного ровно на долю потерянных. Абсолютное время
     * от этого не зависит: потеря тика сдвигает момент проверки, а не сам
     * дедлайн. */
    uint64_t wait_deadline_ns;
    uint8_t wait_timeout_armed;     /* Поток находится в timeout-list ожидания */
    uint8_t wait_timed_out;         /* Поток разбужен по timeout waitq */

    /* Замок состояния планирования — то, чего битам не хватало.
     *
     * Биты TH_* сами по себе атомарны, но инварианты, которые рвались в
     * гонках, — составные: «снял TH_WAIT и положил в очередь», «вижу
     * готовность и отсутствие в очереди — кладу». CAS по одному полю такие
     * переходы не защищает; у XNU thread->state — обычный int под
     * per-thread spinlock (thread_lock()), и корректность держится на нём.
     *
     * Под этим замком: state, решение «класть ли поток в очередь готовности»
     * (сама очередь — под своим rq_lock). Порядок: waitq_spin -> sched_lock
     * -> rq_lock. */
    spinlock_t sched_lock;
    struct thread* joiner;     /* Поток, ожидающий завершения */
    uint8_t reap_queued;       /* Флаг: поток поставлен в очередь reap */
    uint64_t reap_after_tick;  /* Тик, после которого можно освобождать стек */
    void* arch_specific;       /* Архитектурно-зависимые данные */
    /* Per-thread TLS and POSIX thread support */
    uint64_t tls_fs_base;      /* Per-thread FS base (set via CLONE_SETTLS); 0 = not set */
    uint64_t* clear_tid_ptr;   /* User-space TID address cleared to 0 on exit (CLONE_CHILD_CLEARTID) */

    /* FPU/SSE (x87 + XMM) state, saved and restored on context switch.
     *
     * User code uses SSE (the C library's memcpy/memset, and the compiler's
     * own vectorised stores); the kernel is built -mno-sse and never touches
     * these registers. Without per-thread save/restore, a thread preempted
     * mid-SSE-sequence has its XMM registers clobbered by the next thread that
     * uses them, and resumes computing and storing garbage -- which under
     * -smp showed up as intermittent user-memory corruption.
     *
     * FXSAVE/FXRSTOR need a 16-byte-aligned 512-byte area; the extra 16 bytes
     * let us align inside the struct without depending on the allocator's
     * alignment. fpu_used stays 0 until this thread's state has been saved at
     * least once, so a freshly created thread is given a clean default FPU
     * state on its first run rather than an uninitialised area. */
    uint8_t fpu_area[512 + 16];
    uint8_t fpu_used;
} thread_t;

/* ============================================================================
 * Функции для задач
 * ============================================================================ */

/**
 * Создание новой задачи
 * @return Указатель на задачу или NULL при ошибке
 */
task_t* task_create(void);

/**
 * Удаление задачи
 * @param task Указатель на задачу
 */
void task_destroy(task_t* task);

/* Хуки UNIX-персоналии (реализация — kernel/unix/process/unix_proc.c).
 * Объявлены здесь, чтобы ядро управляло временем жизни proc, не зная его полей. */
proc_t* proc_attach(task_t* task);
void proc_detach(task_t* task);

/**
 * Получение текущей задачи
 * @return Указатель на текущую задачу
 */
task_t* task_get_current(void);

/**
 * Установка текущей задачи
 * @param task Указатель на задачу
 */
void task_set_current(task_t* task);

void task_set_abi(task_t* task, task_abi_t abi);
task_abi_t task_get_abi(const task_t* task);

/**
 * Получение количества потоков задачи
 * @param task Указатель на задачу
 * @return Число потоков
 */
uint32_t task_get_thread_count(const task_t* task);

/**
 * Find task by task_id.
 * @param task_id Numeric task id
 * @return Pointer to task or NULL
 */
task_t* task_find_by_id(uint64_t task_id);

typedef struct {
    uint32_t cache_count;
    uint32_t cache_capacity;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t retired;
    uint64_t poison_failures;
} task_stack_cache_stats_t;

/**
 * Acquire kernel thread stack (reused from cache or freshly allocated).
 * @return Stack base pointer or NULL
 */
void* task_kernel_stack_acquire(void);

/**
 * Retire kernel thread stack for deferred reuse.
 * @param stack Stack base pointer
 * @param size Stack size
 */
void task_kernel_stack_retire(void* stack, size_t size);

/**
 * Get kernel stack cache statistics.
 * @param out_stats Pointer to structure to fill
 * @return 0 on success, negative value on error
 */
int task_get_stack_cache_stats(task_stack_cache_stats_t* out_stats);

/* ============================================================================
 * Функции для потоков
 * ============================================================================ */

/**
 * Создание нового потока
 * @param task Задача, к которой принадлежит поток
 * @param entry Точка входа потока
 * @param arg Аргумент для точки входа
 * @return Указатель на поток или NULL при ошибке
 */
thread_t* thread_create(task_t* task, void (*entry)(void*), void* arg);
thread_t* thread_create_user_clone(task_t* task, const struct interrupt_frame* frame);

/*
 * Create a new user thread in an existing task (for clone/pthreads).
 * The new thread shares the task's address space. It starts at the same
 * return-from-syscall RIP as the calling thread, but with rsp=child_stack
 * and rax=0. If tls_fs_base != 0, it is set as the thread's FS base.
 */
thread_t* thread_create_user_thread(task_t* task,
                                    const struct interrupt_frame* frame,
                                    uint64_t child_stack,
                                    uint64_t tls_fs_base);

/**
 * Удаление потока
 * @param thread Указатель на поток
 */
void thread_destroy(thread_t* thread);

/**
 * Получение текущего потока
 * @return Указатель на текущий поток
 */
thread_t* thread_get_current(void);

/**
 * Установка текущего потока
 * @param thread Указатель на поток
 */
void thread_set_current(thread_t* thread);

/**
 * Переключение на другой поток
 * @param from Текущий поток
 * @param to Новый поток
 */
void thread_switch(thread_t* from, thread_t* to);

/**
 * Установка приоритета потока
 * @param thread Указатель на поток
 * @param priority Новый приоритет
 */
void thread_set_priority(thread_t* thread, uint8_t priority);

/* ============================================================================
 * Task iteration
 * ============================================================================ */

/**
 * Iterate all tasks under the registry lock.
 * fn is called for each task with ctx. fn must not try to acquire
 * the task registry lock (no nested iteration).
 */
typedef void (*task_iter_fn_t)(const task_t* task, void* ctx);
void task_for_each(task_iter_fn_t fn, void* ctx);


/* ============================================================================
 * Состояние потока: предикаты и переходы
 * ============================================================================ */

static inline uint32_t thread_state_get(const thread_t* t)
{
    return __atomic_load_n(&t->state, __ATOMIC_ACQUIRE);
}

static inline bool thread_is_runnable(const thread_t* t)
{
    return (thread_state_get(t) & TH_RUN) != 0u;
}

static inline bool thread_is_waiting(const thread_t* t)
{
    return (thread_state_get(t) & TH_WAIT) != 0u;
}

static inline bool thread_is_dead(const thread_t* t)
{
    return (thread_state_get(t) & TH_DEAD) != 0u;
}

/* Имя для отчётов. Не для решений: решение принимается по битам. */
const char* thread_state_name(const thread_t* t);

/* Прежнее перечисление, только чтобы не менять формат /proc и ps. Выводится
 * из битов и знания процессора о том, кто на нём исполняется. */
thread_state_t thread_state_legacy(const thread_t* t);

#endif /* _RODNIX_CORE_TASK_H */
