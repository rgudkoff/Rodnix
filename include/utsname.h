/**
 * @file utsname.h
 * @brief uname data structure
 */

#ifndef _RODNIX_UTSNAME_H
#define _RODNIX_UTSNAME_H

typedef struct {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[64];
    char machine[32];
} utsname_t;

#endif /* _RODNIX_UTSNAME_H */
