/**
 * @file arch/config.h
 * @brief Common entry point for architecture configuration headers.
 */

#ifndef _RODNIX_ARCH_CONFIG_H
#define _RODNIX_ARCH_CONFIG_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/config.h"
#define ARCH_MACHINE X86_64_MACHINE
#define ARCH_KERNEL_VIRT_BASE X86_64_KERNEL_VIRT_BASE
#define ARCH_PAGE_SIZE_4KB X86_64_PAGE_SIZE_4KB
#define ARCH_PAGE_SIZE_2MB X86_64_PAGE_SIZE_2MB
#define ARCH_PAGE_SIZE_1GB X86_64_PAGE_SIZE_1GB
#define ARCH_PHYS_TO_VIRT(addr) X86_64_PHYS_TO_VIRT(addr)
#define ARCH_VIRT_TO_PHYS(addr) X86_64_VIRT_TO_PHYS(addr)
#define ARCH_USER_CANON_MAX 0x00007FFFFFFFFFFFULL
#elif defined(__aarch64__) || defined(_M_ARM64)
#include "arm64/config.h"
#define ARCH_MACHINE "arm64"
#define ARCH_KERNEL_VIRT_BASE ARM64_KERNEL_VIRT_BASE
#define ARCH_PAGE_SIZE_4KB ARM64_PAGE_SIZE_4KB
#define ARCH_PAGE_SIZE_2MB ARM64_PAGE_SIZE_2MB
#define ARCH_PAGE_SIZE_1GB ARM64_PAGE_SIZE_1GB
#define ARCH_PHYS_TO_VIRT(addr) ARM64_PHYS_TO_VIRT(addr)
#define ARCH_VIRT_TO_PHYS(addr) ARM64_VIRT_TO_PHYS(addr)
#define ARCH_USER_CANON_MAX 0x0000FFFFFFFFFFFFULL
#elif defined(__riscv) && (__riscv_xlen == 64)
#include "riscv64/config.h"
#define ARCH_MACHINE "riscv64"
#define ARCH_USER_CANON_MAX 0x0000FFFFFFFFFFFFULL
#else
#error "Unsupported architecture"
#endif

#endif /* _RODNIX_ARCH_CONFIG_H */
