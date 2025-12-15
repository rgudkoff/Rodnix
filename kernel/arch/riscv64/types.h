/**
 * @file riscv64/types.h
 * @brief Data types for RISC-V64 architecture
 */

#ifndef _RODNIX_ARCH_RISCV64_TYPES_H
#define _RODNIX_ARCH_RISCV64_TYPES_H

#include "../../core/arch_types.h"
#include "../../core/config.h"
#include <stdint.h>

/* ============================================================================
 * RISC-V64 CPU registers
 * ============================================================================ */

typedef struct {
    uint64_t x0;            /* zero - always 0 */
    uint64_t x1;             /* ra - return address */
    uint64_t x2;             /* sp - stack pointer */
    uint64_t x3;             /* gp - global pointer */
    uint64_t x4;             /* tp - thread pointer */
    uint64_t x5, x6, x7;     /* t0-t2 - temporary */
    uint64_t x8;             /* s0/fp - saved/frame pointer */
    uint64_t x9;             /* s1 - saved */
    uint64_t x10, x11;       /* a0-a1 - arguments/return */
    uint64_t x12, x13, x14, x15, x16, x17;  /* a2-a7 - arguments */
    uint64_t x18, x19, x20, x21, x22, x23, x24, x25, x26, x27;  /* s2-s11 - saved */
    uint64_t x28, x29, x30, x31;  /* t3-t6 - temporary */
    uint64_t pc;             /* Program Counter */
    uint64_t status;         /* Status register */
} riscv64_registers_t;

/* ============================================================================
 * RISC-V64 interrupt context
 * ============================================================================ */

typedef struct {
    riscv64_registers_t regs;
    uint64_t cause;          /* Cause register */
    uint64_t tval;           /* Trap Value register */
    uint32_t exception_type; /* Exception type */
} riscv64_interrupt_context_t;

/* ============================================================================
 * RISC-V64 page structure (Sv39/Sv48)
 * ============================================================================ */

typedef struct {
    uint64_t v : 1;          /* Valid */
    uint64_t r : 1;          /* Read */
    uint64_t w : 1;          /* Write */
    uint64_t x : 1;          /* Execute */
    uint64_t u : 1;          /* User */
    uint64_t g : 1;          /* Global */
    uint64_t a : 1;          /* Accessed */
    uint64_t d : 1;          /* Dirty */
    uint64_t rsw : 2;        /* Reserved for Software */
    uint64_t ppn : 44;       /* Physical Page Number */
    uint64_t reserved : 10;
} __attribute__((packed)) riscv64_pte_t;

/* ============================================================================
 * Macros for RISC-V64 page addressing (Sv39)
 * ============================================================================ */

#define RISCV64_LEVEL0_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define RISCV64_LEVEL1_INDEX(addr) (((addr) >> 21) & 0x1FF)
#define RISCV64_LEVEL2_INDEX(addr) (((addr) >> 12) & 0x1FF)
#define RISCV64_PAGE_OFFSET(addr) ((addr) & 0xFFF)

/* ============================================================================
 * RISC-V64 constants
 * ============================================================================ */

#define RISCV64_PAGE_SIZE 4096
#define RISCV64_LARGE_PAGE_SIZE 2097152  /* 2MB */
#define RISCV64_HUGE_PAGE_SIZE 1073741824 /* 1GB */

/* Privileges */
#define RISCV64_PRIV_USER 0
#define RISCV64_PRIV_SUPERVISOR 1
#define RISCV64_PRIV_MACHINE 3

#endif /* _RODNIX_ARCH_RISCV64_TYPES_H */

