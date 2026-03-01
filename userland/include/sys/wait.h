#ifndef _RODNIX_USERLAND_SYS_WAIT_H
#define _RODNIX_USERLAND_SYS_WAIT_H

#include <sys/types.h>

#define WIFEXITED(status) (1)
#define WEXITSTATUS(status) ((status) & 0xFF)

#endif /* _RODNIX_USERLAND_SYS_WAIT_H */
