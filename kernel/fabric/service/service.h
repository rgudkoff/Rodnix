/**
 * @file service.h
 * @brief Fabric service interface
 */

#ifndef _RODNIX_FABRIC_SERVICE_H
#define _RODNIX_FABRIC_SERVICE_H

typedef struct fabric_service {
    const char *name;
    void *ops;           /* Service interface (function table) */
    void *context;       /* Service state */
} fabric_service_t;

#endif /* _RODNIX_FABRIC_SERVICE_H */

