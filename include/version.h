/**
 * @file version.h
 * @brief RodNIX version and build macros
 */

#ifndef _RODNIX_VERSION_H
#define _RODNIX_VERSION_H

/* Semantic version */

#define RODNIX_VERSION_MAJOR  0
#define RODNIX_VERSION_MINOR  1
#define RODNIX_VERSION_PATCH  20

/* Integer form for compile-time version checks. */
#define RODNIX_MKVER(maj, min, pat)  (((maj) << 16) | ((min) << 8) | (pat))
#define RODNIX_VERSION_CODE \
    RODNIX_MKVER(RODNIX_VERSION_MAJOR, RODNIX_VERSION_MINOR, RODNIX_VERSION_PATCH)

/* Internal string helpers */

#define _RDNX_STR(x)   #x
#define _RDNX_XSTR(x)  _RDNX_STR(x)

/* uname(2) fields */

/* Operating system name: uname -s */
#define RODNIX_SYSNAME   "RodNIX"

/* Default hostname: uname -n */
#define RODNIX_NODENAME  "localhost"

/* Hardware architecture: uname -m */
#define RODNIX_MACHINE   "x86_64"

/* Kernel release string: uname -r */
#define RODNIX_RELEASE \
    _RDNX_XSTR(RODNIX_VERSION_MAJOR) "." \
    _RDNX_XSTR(RODNIX_VERSION_MINOR) "." \
    _RDNX_XSTR(RODNIX_VERSION_PATCH)

/* Monotonic build number. */
#define RODNIX_BUILD_NUM  44

/* Kernel version string: uname -v */
#define RODNIX_VERSION \
    "#" _RDNX_XSTR(RODNIX_BUILD_NUM) " SMP PREEMPT " __DATE__ " " __TIME__

/* Boot banner shown during kernel startup. */
#define RODNIX_BANNER \
    "RodNIX Kernel Version " RODNIX_RELEASE \
    ": root:rdnx-" RODNIX_RELEASE "~" _RDNX_XSTR(RODNIX_BUILD_NUM) \
    "/RELEASE_" "X86_64"

/* Feature flags for simple compile-time capability checks. */

#define RODNIX_HAS_SMP           0  /* single-core only for now             */
#define RODNIX_HAS_VFS           1  /* VFS + RAMFS/initrd layer             */
#define RODNIX_HAS_EXT2          1  /* EXT2 read; write path in progress    */
#define RODNIX_HAS_EXT2_WRITE    0  /* truncate/writeback: in progress      */
#define RODNIX_HAS_DEVFS         1  /* /dev via devfs                       */
#define RODNIX_HAS_FORK_EXEC     1  /* fork / exec / waitpid                */
#define RODNIX_HAS_POSIX_SIGNALS 1  /* kill / sigaction / sigreturn         */
#define RODNIX_HAS_PIPES         1  /* pipe / pipe2                         */
#define RODNIX_HAS_MMAP          1  /* mmap / munmap / brk                  */
#define RODNIX_HAS_POLL          1  /* poll / select                        */
#define RODNIX_HAS_FUTEX         1  /* futex(FUTEX_WAIT / FUTEX_WAKE)       */
#define RODNIX_HAS_COW           1  /* copy-on-write groundwork in vm/      */
#define RODNIX_HAS_SYMLINKS      0  /* no symlink support yet               */
#define RODNIX_HAS_NETWORKING    0  /* BSD net stack present, not wired up  */

#endif /* _RODNIX_VERSION_H */
