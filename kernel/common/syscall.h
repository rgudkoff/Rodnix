#ifndef _RODNIX_SYSCALL_H
#define _RODNIX_SYSCALL_H

#include <stdint.h>

#define SYSCALL_VECTOR 0x80
#define SYSCALL_MAX 64

typedef uint64_t (*syscall_fn_t)(uint64_t a1,
                                 uint64_t a2,
                                 uint64_t a3,
                                 uint64_t a4,
                                 uint64_t a5,
                                 uint64_t a6);

enum {
    SYS_NOP = 0,
    SYS_TEST_SLEEP = 62,
    /* Reserved for legacy in-kernel compatibility only; userland must use POSIX. */
    SYS_WRITE = 63,
};

void syscall_init(void);
int syscall_register(uint32_t num, syscall_fn_t fn);
uint64_t syscall_dispatch(uint64_t num,
                          uint64_t a1,
                          uint64_t a2,
                          uint64_t a3,
                          uint64_t a4,
                          uint64_t a5,
                          uint64_t a6);
void syscall_account_int80(void);
void syscall_account_fast(void);
void syscall_account_entry(uint64_t num, int fast_entry);
uint64_t syscall_get_int80_count(void);
uint64_t syscall_get_fast_count(void);
uint64_t syscall_get_int80_count_for_num(uint64_t num);
uint64_t syscall_get_fast_count_for_num(uint64_t num);

#endif /* _RODNIX_SYSCALL_H */
