/**
 * @file error.h
 * @brief Common error codes
 */

#ifndef _RODNIX_ERROR_H
#define _RODNIX_ERROR_H

#include <stdint.h>

/* Success */
#define RDNX_OK 0

/* Generic errors (negative) */
#define RDNX_E_GENERIC      (-1)
#define RDNX_E_INVALID      (-2)
#define RDNX_E_NOMEM        (-3)
#define RDNX_E_NOTFOUND     (-4)
#define RDNX_E_BUSY         (-5)
#define RDNX_E_DENIED       (-6)
#define RDNX_E_UNSUPPORTED  (-7)
#define RDNX_E_TIMEOUT      (-8)
#define RDNX_E_AGAIN        (-9)  /* EAGAIN / EWOULDBLOCK — try again later */

#endif /* _RODNIX_ERROR_H */
