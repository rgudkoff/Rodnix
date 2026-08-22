/**
 * @file proc.h
 * @brief POSIX process state (UNIX personality attached to a kernel task)
 *
 * Разделение ответственности:
 *
 *   task_t  (kernel/core/task.h) — объект ядра: адресное пространство,
 *           потоки, планирование, реестр задач, refcount.
 *   proc_t  (этот файл)          — UNIX-персоналия задачи: креды, таблица
 *           дескрипторов, cwd, сигналы, статус завершения, brk/mmap-раскладка.
 *
 * Связь 1:1 (task->proc / proc->task) и создаётся вместе с задачей, но типы
 * разведены намеренно: файл ядра, который не включает этот заголовок,
 * физически не может обратиться к POSIX-полям. Это и есть граница.
 *
 * В task_t осознанно оставлены поля, которые выглядят "posix'ными":
 *   - abi          — селектор персоналии, читается arch-кодом входа в trap
 *                    до того, как отработает POSIX-слой;
 *   - tls_fs_base  — состояние регистра FS, восстанавливается на переключении
 *                    контекста (горячий путь планировщика);
 *   - vm_brk_ и vm_mmap_ — раскладка адресного пространства, которой управляет
 *                    mm/vm_map.c; иначе VM-слой пришлось бы тянуть на POSIX.
 */

#ifndef _RODNIX_UNIX_PROC_H
#define _RODNIX_UNIX_PROC_H

#include "../core/task.h"
#include <stdint.h>

#define PROC_MAX_FD 32
#define PROC_CWD_MAX 256
#define PROC_MAX_SUPP_GROUPS 16
#define PROC_NSIG 32

/* Диспозиция сигнала (POSIX sigaction в терминах user ABI). */
typedef struct proc_sigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
} proc_sigaction_t;

/* Снимок пользовательского контекста на время работы обработчика сигнала. */
typedef struct proc_sigframe {
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
} proc_sigframe_t;

struct rdnx_proc {
    task_t* task;                 /* Владеющая задача ядра (1:1) */

    /* --- Идентичность и права --- */
    uint32_t uid;                 /* Реальный UID */
    uint32_t gid;                 /* Реальный GID */
    uint32_t euid;                /* Эффективный UID */
    uint32_t egid;                /* Эффективный GID */
    uint32_t supp_groups[PROC_MAX_SUPP_GROUPS];
    uint32_t supp_group_count;
    uint64_t session_id;          /* POSIX session id */
    uint64_t process_group_id;    /* POSIX process group id */
    uint16_t umask;               /* Маска прав создания файлов */

    /* --- Дескрипторы и файловая позиция процесса --- */
    void* fd_table[PROC_MAX_FD];   /* rdnx_file_t* — описание открытого файла */
    uint8_t fd_flags[PROC_MAX_FD]; /* FD_CLOEXEC — свойство дескриптора */
    char cwd[PROC_CWD_MAX];       /* Текущая рабочая директория */

    /* --- Завершение и ожидание --- */
    int32_t exit_code;            /* Код завершения */
    uint8_t exited;               /* Завершён через posix_exit */
    uint8_t waited;               /* Статус уже забран waitpid */
    waitq_t child_waitq;          /* Родитель спит здесь в wait()/waitpid() */

    /* --- Сигналы --- */
    proc_sigaction_t sigaction[PROC_NSIG];
    uint32_t sig_pending;
    uint8_t sig_in_handler;
    proc_sigframe_t sig_saved;
};

/* ============================================================================
 * Жизненный цикл
 * ============================================================================ */

/**
 * Создать и присоединить UNIX-персоналию к задаче.
 * Вызывается из task_create(); повторный вызов возвращает существующий proc.
 * @return proc или NULL при нехватке памяти
 */
proc_t* proc_attach(task_t* task);

/**
 * Освободить дескрипторы и уничтожить персоналию задачи.
 * Вызывается из task_destroy().
 */
void proc_detach(task_t* task);

/* ============================================================================
 * Доступ
 * ============================================================================ */

/**
 * Персоналия задачи.
 * const у task_t относится к самой задаче, а не к присоединённому proc.
 */
static inline proc_t* task_proc(const task_t* task)
{
    return task ? task->proc : (proc_t*)0;
}

/** Персоналия текущей задачи (NULL, если задачи нет). */
proc_t* proc_current(void);

/* ============================================================================
 * Креды
 * ============================================================================ */

void proc_set_ids(proc_t* proc, uint32_t uid, uint32_t gid, uint32_t euid, uint32_t egid);
int proc_set_supp_groups(proc_t* proc, const uint32_t* gids, uint32_t count);
uint32_t proc_get_supp_group_count(const proc_t* proc);
int proc_copy_supp_groups(const proc_t* proc, uint32_t* out_gids, uint32_t max_count);
int proc_in_group(const proc_t* proc, uint32_t gid);
uint32_t proc_get_euid(const proc_t* proc);
uint32_t proc_get_egid(const proc_t* proc);

/* ============================================================================
 * Дескрипторы
 * ============================================================================ */

/** Занять свободный слот дескриптора. @return fd >= 0 или RDNX_E_* */
int proc_fd_alloc(proc_t* proc, void* handle);

/** Хэндл по дескриптору или NULL. */
void* proc_fd_get(proc_t* proc, int fd);

/** Освободить слот дескриптора. @return RDNX_OK или RDNX_E_* */
int proc_fd_close(proc_t* proc, int fd);

/* ============================================================================
 * Иерархия процессов
 * ============================================================================ */

/**
 * Найти потомка задачи parent_task_id.
 * @param require_exited учитывать только завершившихся
 * @param include_waited включать тех, чей статус уже забран
 */
task_t* proc_find_child_by_parent(uint64_t parent_task_id, int require_exited, int include_waited);

#endif /* _RODNIX_UNIX_PROC_H */
