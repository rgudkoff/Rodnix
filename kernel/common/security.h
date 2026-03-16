#ifndef _RODNIX_COMMON_SECURITY_H
#define _RODNIX_COMMON_SECURITY_H

#include <stdint.h>

#define SEC_OK   0
#define SEC_DENY (-1)

/* POSIX permission bit constants (for inode mode). */
#define S_IRUSR 0400u
#define S_IWUSR 0200u
#define S_IXUSR 0100u
#define S_IRGRP 0040u
#define S_IWGRP 0020u
#define S_IXGRP 0010u
#define S_IROTH 0004u
#define S_IWOTH 0002u
#define S_IXOTH 0001u

#define S_IRWXU (S_IRUSR|S_IWUSR|S_IXUSR)
#define S_IRWXG (S_IRGRP|S_IWGRP|S_IXGRP)
#define S_IRWXO (S_IROTH|S_IWOTH|S_IXOTH)

/* Default modes for new filesystem objects. */
#define VFS_MODE_FILE_DEFAULT  (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH) /* 0644 */
#define VFS_MODE_DIR_DEFAULT   (S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) /* 0755 */

/* Access request flags passed to security_vfs_access(). */
#define SEC_ACCESS_READ  1
#define SEC_ACCESS_WRITE 2
#define SEC_ACCESS_EXEC  4

int security_init(void);
int security_check_euid(uint32_t required_uid);

/**
 * security_vfs_access — DAC check for opening an inode.
 *
 * @param inode_mode  inode->mode (permission bits)
 * @param inode_uid   inode->uid
 * @param inode_gid   inode->gid
 * @param access      bitset of SEC_ACCESS_READ / _WRITE / _EXEC
 * @param caller_euid effective UID of the calling process
 * @param caller_egid effective GID of the calling process
 *
 * Returns SEC_OK if access is granted, SEC_DENY otherwise.
 * Root (euid == 0) always gets SEC_OK.
 */
int security_vfs_access(uint16_t inode_mode,
                        uint32_t inode_uid,
                        uint32_t inode_gid,
                        int      access,
                        uint32_t caller_euid,
                        uint32_t caller_egid);

#endif /* _RODNIX_COMMON_SECURITY_H */
