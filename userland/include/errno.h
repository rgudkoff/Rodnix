#ifndef _RODNIX_USERLAND_ERRNO_H
#define _RODNIX_USERLAND_ERRNO_H

#include <sys/errno.h>

extern int errno;

/* Translate internal RDNX kernel error codes to POSIX errno values.
 * Matches include/error.h:
 *   -1  RDNX_E_GENERIC      → EIO
 *   -2  RDNX_E_INVALID      → EINVAL
 *   -3  RDNX_E_NOMEM        → ENOMEM
 *   -4  RDNX_E_NOTFOUND     → ENOENT
 *   -5  RDNX_E_BUSY         → EBUSY
 *   -6  RDNX_E_DENIED       → EACCES  (callers may remap to EPERM/ECHILD)
 *   -7  RDNX_E_UNSUPPORTED  → ENOSYS
 *   -8  RDNX_E_TIMEOUT      → ETIMEDOUT
 *   -9  RDNX_E_AGAIN        → EAGAIN
 *   -10 RDNX_E_EXIST        → EEXIST
 */
static inline int rdnx_errno_from_status(long r)
{
    switch ((int)r) {
        case -2:  return EINVAL;
        case -3:  return ENOMEM;
        case -4:  return ENOENT;
        case -5:  return EBUSY;
        case -6:  return EACCES;
        case -7:  return ENOSYS;
        case -8:  return ETIMEDOUT;
        case -9:  return EAGAIN;
        case -10: return EEXIST;
        default:  return EIO;
    }
}

#endif /* _RODNIX_USERLAND_ERRNO_H */
