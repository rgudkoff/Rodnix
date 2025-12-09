#ifndef _RODNIX_SCHEDULER_H
#define _RODNIX_SCHEDULER_H

#include "types.h"

/* Состояния процесса */
typedef enum {
    PROCESS_STATE_RUNNING = 0,
    PROCESS_STATE_READY,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_SLEEPING,
    PROCESS_STATE_ZOMBIE,
    PROCESS_STATE_DEAD
} process_state_t;

/* Структура процесса */
typedef struct process {
    uint32_t pid;                    /* Process ID */
    uint32_t ppid;                   /* Parent Process ID */
    process_state_t state;           /* Состояние процесса */
    uint32_t* page_dir;              /* Page directory процесса */
    uint32_t stack_top;              /* Верх стека */
    uint32_t stack_bottom;           /* Низ стека */
    uint32_t entry_point;            /* Точка входа */
    uint32_t priority;               /* Приоритет (0-255) */
    uint64_t time_slice;             /* Оставшееся время кванта */
    uint64_t total_time;             /* Общее время выполнения */
    struct process* next;             /* Следующий процесс в списке */
} process_t;

/* Инициализация планировщика */
int scheduler_init(void);

/* Создание процесса */
process_t* process_create(uint32_t entry_point, uint32_t stack_size, uint32_t priority);

/* Удаление процесса */
int process_destroy(uint32_t pid);

/* Переключение контекста */
void schedule(void);

/* Блокировка текущего процесса */
void process_block(process_t* proc);

/* Разблокировка процесса */
void process_unblock(process_t* proc);

/* Получение текущего процесса */
process_t* get_current_process(void);

/* Получение процесса по PID */
process_t* process_find(uint32_t pid);

/* Установка приоритета процесса */
int process_set_priority(uint32_t pid, uint32_t priority);

#endif

