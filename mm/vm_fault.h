#ifndef _RODNIX_VM_FAULT_H
#define _RODNIX_VM_FAULT_H

#include <stdint.h>
#include "../kernel/core/task.h"

/*
 * Счётчики пути отказа. Не отладка, а измеритель: обещание этапа 7 —
 * «RT-поток не берёт ни одного отказа» — проверяется числом, а не верой,
 * и per-thread половина этого числа лежит в thread_t.fault_count.
 */
typedef struct vm_fault_stats {
    uint64_t faults;      /* всего обработанных отказов */
    uint64_t zero_fill;   /* свежая нулевая страница */
    uint64_t pager_read;  /* страница наполнена задником файла */
    uint64_t cow_copy;    /* копия при записи */
    uint64_t adopted;     /* проиграна гонка вставки: принята чужая страница */
    uint64_t spurious;    /* отображение уже стояло: отказ решён другим */
} vm_fault_stats_t;

int vm_fault_handle(task_t* task, uint64_t fault_addr, uint64_t err_code, uint64_t rip);
void vm_fault_get_stats(vm_fault_stats_t* out);

/*
 * Сильное обещание реального времени (этап 7): каждая страница диапазона
 * приводится в память СЕЙЧАС — включая разрыв COW для записываемых
 * отображений — и закрепляется. После успешного возврата ни одно обращение
 * к диапазону не берёт отказа. Живёт в vm_fault.c, потому что «привести
 * страницу» — это и есть машинерия отказа, вызванная заранее.
 */
int vm_task_mlock(task_t* task, uint64_t addr, uint64_t len);
int vm_task_munlock(task_t* task, uint64_t addr, uint64_t len);

#endif /* _RODNIX_VM_FAULT_H */
