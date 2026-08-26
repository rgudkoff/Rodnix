#ifndef _RODNIX_VM_RECLAIM_H
#define _RODNIX_VM_RECLAIM_H

#include <stdint.h>
#include "../kernel/core/task.h"

/* Уровни давления памяти. */
enum {
    VM_PRESSURE_NONE     = 0,
    VM_PRESSURE_WARN     = 1,  /* свободного < 15%: время сбрасывать кэши */
    VM_PRESSURE_CRITICAL = 2,  /* свободного < 5%: возврат уже идёт */
};

int vm_pressure_level(void);

/*
 * Вернуть аллокатору до target страниц, классами снизу вверх по цене
 * ошибки: VOLATILE (выбросить по контракту) -> CACHE -> NORMAL (файловое,
 * с записью в задник). PROJECT и RT не трогаются никогда. Возвращает
 * число освобождённых страниц.
 */
uint32_t vm_reclaim_run(uint32_t target);

/*
 * Учёт задачи, два числа на два разных вопроса (mm_redesign.md):
 * charged — «чем пользуется процесс»: за разделяемую страницу платит
 * каждый держатель, сумма по системе больше физической памяти сознательно;
 * reclaimable — «что освободит его смерть»: страницы, для которых он
 * последний держатель. Убивать по charged — убивать не того.
 */
void vm_task_mem_account(task_t* task, uint64_t* charged, uint64_t* reclaimable);

#endif /* _RODNIX_VM_RECLAIM_H */
