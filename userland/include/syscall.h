/**
 * @file syscall.h
 * @brief Minimal syscall wrappers for userland
 */

#ifndef _RODNIX_USERLAND_SYSCALL_H
#define _RODNIX_USERLAND_SYSCALL_H

#include <stdint.h>

#ifndef RDNX_USE_FAST_SYSCALL
#define RDNX_USE_FAST_SYSCALL 0
#endif

static inline long rdnx_syscall6(long n,
                                 long a1,
                                 long a2,
                                 long a3,
                                 long a4,
                                 long a5,
                                 long a6)
{
    long ret;
#if RDNX_USE_FAST_SYSCALL
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "cc", "memory"
    );
#else
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "memory"
    );
#endif
    return ret;
}

static inline long rdnx_syscall0(long n)
{
    long ret;
#if RDNX_USE_FAST_SYSCALL
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "cc", "memory"
    );
#else
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n)
        : "memory"
    );
#endif
    return ret;
}

static inline long rdnx_syscall1(long n, long a1)
{
    long ret;
#if RDNX_USE_FAST_SYSCALL
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "cc", "memory"
    );
#else
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "memory"
    );
#endif
    return ret;
}

static inline long rdnx_syscall2(long n, long a1, long a2)
{
    long ret;
#if RDNX_USE_FAST_SYSCALL
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "cc", "memory"
    );
#else
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "memory"
    );
#endif
    return ret;
}

static inline long rdnx_syscall3(long n, long a1, long a2, long a3)
{
    long ret;
#if RDNX_USE_FAST_SYSCALL
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "cc", "memory"
    );
#else
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
#endif
    return ret;
}

static inline long rdnx_syscall4(long n, long a1, long a2, long a3, long a4)
{
    long ret;
    register long r10 __asm__("r10") = a4;
#if RDNX_USE_FAST_SYSCALL
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "cc", "memory"
    );
#else
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "memory"
    );
#endif
    return ret;
}

static inline long rdnx_syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
#if RDNX_USE_FAST_SYSCALL
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "cc", "memory"
    );
#else
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "memory"
    );
#endif
    return ret;
}

#endif /* _RODNIX_USERLAND_SYSCALL_H */
