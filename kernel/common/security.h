#ifndef _RODNIX_COMMON_SECURITY_H
#define _RODNIX_COMMON_SECURITY_H

#include <stdint.h>

#define SEC_OK 0
#define SEC_DENY -1

int security_init(void);
int security_check_euid(uint32_t required_uid);

#endif /* _RODNIX_COMMON_SECURITY_H */
