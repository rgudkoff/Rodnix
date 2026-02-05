#include "security.h"
#include "../core/task.h"

int security_init(void)
{
    return 0;
}

int security_check_euid(uint32_t required_uid)
{
    task_t* task = task_get_current();
    if (!task) {
        return SEC_DENY;
    }
    if (task->euid == 0 || task->euid == required_uid) {
        return SEC_OK;
    }
    return SEC_DENY;
}
