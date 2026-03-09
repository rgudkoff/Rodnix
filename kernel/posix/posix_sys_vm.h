#ifndef _RODNIX_POSIX_SYS_VM_H
#define _RODNIX_POSIX_SYS_VM_H

#include <stdint.h>

uint64_t posix_mmap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);
uint64_t posix_munmap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);
uint64_t posix_msync(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);
uint64_t posix_brk(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6);

#endif /* _RODNIX_POSIX_SYS_VM_H */
