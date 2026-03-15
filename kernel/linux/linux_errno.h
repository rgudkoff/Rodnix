#ifndef _RODNIX_LINUX_ERRNO_H
#define _RODNIX_LINUX_ERRNO_H

/* Minimal guest ABI errno subset for syscall return values. */
#define LINUX_EPERM 1
#define LINUX_ENOENT 2
#define LINUX_EIO 5
#define LINUX_EBADF 9
#define LINUX_EAGAIN 11
#define LINUX_ENOMEM 12
#define LINUX_EACCES 13
#define LINUX_EBUSY 16
#define LINUX_EEXIST 17
#define LINUX_ENOTDIR 20
#define LINUX_EISDIR 21
#define LINUX_EINVAL 22
#define LINUX_ENFILE 23
#define LINUX_EMFILE 24
#define LINUX_ENOSPC 28
#define LINUX_ESPIPE 29
#define LINUX_EROFS 30
#define LINUX_ENOSYS 38
#define LINUX_ENOTEMPTY 39
#define LINUX_ETIMEDOUT 110

int linux_errno_from_rdnx(int rdnx_error);

#endif /* _RODNIX_LINUX_ERRNO_H */
