#include "security.h"
#include "core/task.h"
#include "unix/proc.h"

int security_init(void)
{
    return 0;
}

int security_check_euid(uint32_t required_uid)
{
    proc_t* proc = proc_current();
    if (!proc) {
        return SEC_DENY;
    }
    if (proc->euid == 0 || proc->euid == required_uid) {
        return SEC_OK;
    }
    return SEC_DENY;
}

int security_vfs_access(uint16_t inode_mode,
                        uint32_t inode_uid,
                        uint32_t inode_gid,
                        int      access,
                        const task_t* caller)
{
    const proc_t* caller_proc = task_proc(caller);
    uint32_t caller_euid = proc_get_euid(caller_proc);

    /* Root bypasses DAC. */
    if (caller_euid == 0) {
        return SEC_OK;
    }

    /* Determine which permission triad applies. */
    uint16_t mask = 0;
    if (caller_euid == inode_uid) {
        /* owner triad */
        if (access & SEC_ACCESS_READ)  mask |= S_IRUSR;
        if (access & SEC_ACCESS_WRITE) mask |= S_IWUSR;
        if (access & SEC_ACCESS_EXEC)  mask |= S_IXUSR;
    } else if (proc_in_group(caller_proc, inode_gid)) {
        /* group triad */
        if (access & SEC_ACCESS_READ)  mask |= S_IRGRP;
        if (access & SEC_ACCESS_WRITE) mask |= S_IWGRP;
        if (access & SEC_ACCESS_EXEC)  mask |= S_IXGRP;
    } else {
        /* other triad */
        if (access & SEC_ACCESS_READ)  mask |= S_IROTH;
        if (access & SEC_ACCESS_WRITE) mask |= S_IWOTH;
        if (access & SEC_ACCESS_EXEC)  mask |= S_IXOTH;
    }

    return ((inode_mode & mask) == mask) ? SEC_OK : SEC_DENY;
}
