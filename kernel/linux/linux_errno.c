#include "linux_errno.h"
#include "../../include/error.h"

/*
 * FreeBSD linuxulator maps native errno to Linux errno.
 * RodNIX currently has its own compact internal error set, so this function
 * maps RDNX_E_* to Linux-visible errno values.
 */
int linux_errno_from_rdnx(int rdnx_error)
{
    switch (rdnx_error) {
    case RDNX_OK:
        return 0;
    case RDNX_E_INVALID:
        return LINUX_EINVAL;
    case RDNX_E_NOMEM:
        return LINUX_ENOMEM;
    case RDNX_E_NOTFOUND:
        return LINUX_ENOENT;
    case RDNX_E_BUSY:
        return LINUX_EBUSY;
    case RDNX_E_DENIED:
        return LINUX_EACCES;
    case RDNX_E_UNSUPPORTED:
        return LINUX_ENOSYS;
    case RDNX_E_TIMEOUT:
        return LINUX_ETIMEDOUT;
    default:
        return LINUX_EIO;
    }
}
