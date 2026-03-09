#ifndef _RODNIX_COMPAT_SYS_TYPES_H
#define _RODNIX_COMPAT_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef int64_t quad_t;
typedef uint64_t u_quad_t;
typedef char* caddr_t;
typedef long register_t;
typedef uintptr_t vm_offset_t;
typedef uintptr_t vm_paddr_t;

typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long off_t;
typedef unsigned long size_t_compat;

typedef uintptr_t bus_addr_t;
typedef uintptr_t bus_size_t;

#endif
