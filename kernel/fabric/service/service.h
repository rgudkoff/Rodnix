/**
 * @file service.h
 * @brief Fabric service interface
 */

#ifndef _RODNIX_FABRIC_SERVICE_H
#define _RODNIX_FABRIC_SERVICE_H

#include "../../../include/abi.h"

typedef struct fabric_service {
    rdnx_abi_header_t hdr;
    const char *name;
    void *ops;           /* Service interface (function table) */
    void *context;       /* Service state */
} fabric_service_t;

#endif /* _RODNIX_FABRIC_SERVICE_H */
