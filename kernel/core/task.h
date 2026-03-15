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
#include <bsd/sys/queue.h>
#include <bsd/sys/tree.h>
#include <stdint.h>
#include <stdbool.h>

struct interrupt_frame;

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

/* ============================================================================
 * Приоритет
 * ============================================================================ */

#define PRIORITY_MIN 0
#define PRIORITY_MAX 255
#define PRIORITY_DEFAULT 128

/* ============================================================================
 * File descriptors (minimal)
 * ============================================================================ */

#define TASK_MAX_FD 32
#define TASK_CWD_MAX 256

/* ============================================================================
 * Scheduling class
 * ============================================================================ */

enum {
    SCHED_CLASS_TIMESHARE = 0,
    SCHED_CLASS_REALTIME  = 1,
};

/* ============================================================================
 * Задача (адресное пространство + ресурсы)
 * ============================================================================ */

typedef struct task {
    uint64_t task_id;          /* Уникальный ID задачи */
    uint64_t parent_task_id;   /* Родительская задача (0 для kernel/orphan) */
    void* address_space;       /* Адресное пространство (vm_map) */
    void* vm_map;              /* VM map (unix-style user virtual memory map) */
    uint64_t vm_brk_base;      /* Base of brk() region (end of data/bss image) */
    uint64_t vm_brk_end;       /* Current program break */
    uint64_t vm_mmap_base;     /* Base for mmap() allocations */
    uint64_t vm_mmap_hint;     /* Next mmap() search hint */
    task_state_t state;        /* Состояние задачи */
    uint32_t uid;              /* Реальный UID */
    uint32_t gid;              /* Реальный GID */
    uint32_t euid;             /* Эффективный UID */
    uint32_t egid;             /* Эффективный GID */
    uint16_t umask;            /* process umask (POSIX bits) */
    void* fd_table[TASK_MAX_FD]; /* Таблица файловых дескрипторов (vfs_file_t*) */
    uint8_t fd_flags[TASK_MAX_FD]; /* Флаги дескрипторов (например, FD_CLOEXEC) */
    uint8_t fd_kind[TASK_MAX_FD];  /* Тип дескриптора (unix fd kind) */
    char cwd[TASK_CWD_MAX];     /* Текущая рабочая директория */
    int32_t exit_code;         /* Код завершения процесса */
    uint8_t exited;            /* Процесс завершен через posix_exit */
    uint8_t waited;            /* Статус уже забран waitpid */
    struct {
        uint64_t handler;
        uint64_t flags;
        uint64_t restorer;
        uint64_t mask;
    } sigaction[32];
    uint32_t sig_pending;
    uint8_t sig_in_handler;
    uint8_t abi;               /* task_abi_t */
    uint64_t tls_fs_base;      /* userspace FS base (arch_prctl/linux ABI) */
    struct {
        uint64_t rip;
        uint64_t rsp;
        uint64_t rflags;
        uint64_t rax;
        uint64_t rbx;
        uint64_t rcx;
        uint64_t rdx;
        uint64_t rsi;
        uint64_t rdi;
        uint64_t rbp;
        uint64_t r8;
        uint64_t r9;
        uint64_t r10;
        uint64_t r11;
        uint64_t r12;
        uint64_t r13;
        uint64_t r14;
        uint64_t r15;
    } sig_saved;
    struct thread* main_thread;/* Основной поток процесса */
    TAILQ_HEAD(thread_list, thread) threads; /* Список всех потоков задачи */
    uint32_t thread_count;     /* Количество потоков задачи */
    uint32_t ref_count;        /* Счетчик ссылок */
    struct task* next_all;     /* Связный список всех задач */
    RB_ENTRY(task) task_id_link; /* Узел task_id-индекса */
    void* arch_specific;       /* Архитектурно-зависимые данные */
} task_t;

/* ============================================================================
 * Поток (thread)
 * ============================================================================ */

typedef struct thread {
    uint64_t thread_id;        /* Уникальный ID потока */
    task_t* task;              /* Задача, к которой принадлежит поток */
    thread_context_t context;   /* Контекст выполнения */
    thread_state_t state;      /* Состояние потока */
    uint8_t sched_class;       /* Класс планирования */
    uint8_t priority;          /* Приоритет потока */
    uint8_t base_priority;     /* Базовый приоритет */
    int16_t dyn_priority;      /* Динамический приоритет (с учётом boost/penalty) */
    int16_t inherited_priority;/* Приоритет по наследованию */
    uint8_t has_inherited;     /* Флаг наследования */
    int16_t inherit_stack[8];  /* Стек наследованных приоритетов */
    uint8_t inherit_depth;     /* Глубина стека наследования */
    uint8_t has_inherit_overflow; /* Флаг переполнения стека наследования */
    uint32_t sched_usage;      /* Счётчик использования CPU */
    uint64_t last_sleep_tick;  /* Последний тик блокировки */
    void (*entry)(void*);      /* Точка входа потока */
    void* arg;                 /* Аргумент для точки входа */
    void* stack;               /* Указатель на стек */
    size_t stack_size;         /* Размер стека */
    TAILQ_ENTRY(thread) task_link;  /* Узел списка потоков задачи (task_t.threads) */
    TAILQ_ENTRY(thread) sched_link; /* Узел ready-очереди планировщика */
    uint8_t ready_queued;      /* Поток находится в ready queue */
    TAILQ_ENTRY(thread) wait_link;  /* Узел waitq-очереди */
    TAILQ_ENTRY(thread) wait_timeout_link; /* Узел глобального timeout-list ожидания */
    struct waitq* waitq_owner;      /* Текущая waitq, если поток ожидает */
    uint64_t wait_deadline_tick;    /* Дедлайн ожидания в тиках (0=без дедлайна) */
    uint8_t wait_timeout_armed;     /* Поток находится в timeout-list ожидания */
    uint8_t wait_timed_out;         /* Поток разбужен по timeout waitq */
    struct thread* joiner;     /* Поток, ожидающий завершения */
    uint8_t reap_queued;       /* Флаг: поток поставлен в очередь reap */
    uint64_t reap_after_tick;  /* Тик, после которого можно освобождать стек */
    void* arch_specific;       /* Архитектурно-зависимые данные */
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

/**
 * Установка идентификаторов пользователя/группы
 * @param task Указатель на задачу
 * @param uid Реальный UID
 * @param gid Реальный GID
 * @param euid Эффективный UID
 * @param egid Эффективный GID
 */
void task_set_ids(task_t* task, uint32_t uid, uint32_t gid, uint32_t euid, uint32_t egid);
void task_set_abi(task_t* task, task_abi_t abi);
task_abi_t task_get_abi(const task_t* task);

/* ============================================================================
 * File descriptors helpers
 * ============================================================================ */

/**
 * Allocate a new file descriptor in task table
 * @return fd >= 0 on success, negative value on error
 */
int task_fd_alloc(task_t* task, void* handle);

/**
 * Get handle by fd
 * @return handle or NULL
 */
void* task_fd_get(task_t* task, int fd);

/**
 * Close and clear fd
 * @return 0 on success, negative value on error
 */
int task_fd_close(task_t* task, int fd);

/**
 * Получение эффективного UID
 * @param task Указатель на задачу
 * @return euid
 */
uint32_t task_get_euid(const task_t* task);

/**
 * Получение эффективного GID
 * @param task Указатель на задачу
 * @return egid
 */
uint32_t task_get_egid(const task_t* task);

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
 * Блокировка потока
 * @param thread Указатель на поток
 */
void thread_block(thread_t* thread);

/**
 * Разблокировка потока
 * @param thread Указатель на поток
 */
void thread_unblock(thread_t* thread);

/**
 * Установка приоритета потока
 * @param thread Указатель на поток
 * @param priority Новый приоритет
 */
void thread_set_priority(thread_t* thread, uint8_t priority);

#endif /* _RODNIX_CORE_TASK_H */
