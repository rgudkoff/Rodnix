#ifndef _RODNIX_USERLAND_SYS_FCNTL_H
#define _RODNIX_USERLAND_SYS_FCNTL_H

/* POSIX/BSD-style access mode bits */
#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_ACCMODE 0x0003

/* Create/truncate bits */
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

#endif /* _RODNIX_USERLAND_SYS_FCNTL_H */
