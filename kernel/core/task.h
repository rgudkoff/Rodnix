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
#include <stdint.h>
#include <stdbool.h>

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
 * Задача (адресное пространство + ресурсы)
 * ============================================================================ */

typedef struct task {
    uint64_t task_id;          /* Уникальный ID задачи */
    void* address_space;       /* Адресное пространство (vm_map) */
    task_state_t state;        /* Состояние задачи */
    uint32_t uid;              /* Реальный UID */
    uint32_t gid;              /* Реальный GID */
    uint32_t euid;             /* Эффективный UID */
    uint32_t egid;             /* Эффективный GID */
    uint32_t ref_count;        /* Счетчик ссылок */
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
    uint8_t priority;          /* Приоритет потока */
    uint8_t base_priority;     /* Базовый приоритет */
    int16_t dyn_priority;      /* Динамический приоритет (с учётом boost/penalty) */
    uint32_t sched_usage;      /* Счётчик использования CPU */
    uint64_t last_sleep_tick;  /* Последний тик блокировки */
    void (*entry)(void*);      /* Точка входа потока */
    void* arg;                 /* Аргумент для точки входа */
    void* stack;               /* Указатель на стек */
    size_t stack_size;         /* Размер стека */
    struct thread* sched_next; /* Следующий в очереди планировщика */
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
