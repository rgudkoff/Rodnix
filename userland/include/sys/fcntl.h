#ifndef _RODNIX_USERLAND_SYS_FCNTL_H
#define _RODNIX_USERLAND_SYS_FCNTL_H

/*
 * Generated from FreeBSD headers by scripts/sync_bsd_abi_headers.py.
 * Source: third_party/bsd/sys/sys/fcntl.h
 */
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_ACCMODE 0x0003

#define O_NONBLOCK 0x0004
#define O_APPEND   0x0008
#define O_SYNC     0x0080
#define O_NOFOLLOW 0x0100
#define O_CREAT    0x0200
#define O_TRUNC    0x0400
#define O_EXCL     0x0800
#define O_NOCTTY   0x8000
#define O_CLOEXEC  0x00100000

#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

#define FD_CLOEXEC 1

#define AT_FDCWD -100

#endif /* _RODNIX_USERLAND_SYS_FCNTL_H */
