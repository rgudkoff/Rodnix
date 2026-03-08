#ifndef _RODNIX_USERLAND_SYS_MMAN_H
#define _RODNIX_USERLAND_SYS_MMAN_H

/*
 * Generated from FreeBSD headers by scripts/sync_bsd_abi_headers.py.
 * Source: third_party/bsd/freebsd-src/sys/sys/mman.h
 */
#define PROT_NONE   0x0000
#define PROT_READ   0x0001
#define PROT_WRITE  0x0002
#define PROT_EXEC   0x0004

#define MAP_SHARED   0x0001
#define MAP_PRIVATE  0x0002
#define MAP_FIXED    0x0010
#define MAP_ANON     0x1000
#define MAP_ANONYMOUS MAP_ANON

#define MS_SYNC       0x0000
#define MS_ASYNC      0x0001
#define MS_INVALIDATE 0x0002

#endif /* _RODNIX_USERLAND_SYS_MMAN_H */
