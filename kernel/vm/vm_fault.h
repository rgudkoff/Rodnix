#ifndef _RODNIX_VM_FAULT_H
#define _RODNIX_VM_FAULT_H

#include <stdint.h>
#include "../core/task.h"

int vm_fault_handle(task_t* task, uint64_t fault_addr, uint64_t err_code, uint64_t rip);

#endif /* _RODNIX_VM_FAULT_H */

