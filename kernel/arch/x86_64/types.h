/**
 * @file x86_64/types.h
 * @brief Data types for x86_64 architecture
 */

#ifndef _RODNIX_ARCH_X86_64_TYPES_H
#define _RODNIX_ARCH_X86_64_TYPES_H

#include "../../core/arch_types.h"
#include "../../core/config.h"
#include <stdint.h>

/* ============================================================================
 * x86_64 CPU registers
 * ============================================================================ */

typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint16_t cs;
    uint16_t ds;
    uint16_t es;
    uint16_t fs;
    uint16_t gs;
    uint16_t ss;
} x86_64_registers_t;

/* ============================================================================
 * x86_64 interrupt context
 * ============================================================================ */

typedef struct {
    x86_64_registers_t regs;
    uint64_t error_code;
    uint32_t vector;
} x86_64_interrupt_context_t;

/* ============================================================================
 * x86_64 page structure (4KB)
 * ============================================================================ */

typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t pat : 1;
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;
    uint64_t available2 : 11;
    uint64_t nx : 1;
} __attribute__((packed)) x86_64_pte_t;

/* ============================================================================
 * PML4 Entry (Page Map Level 4)
 * ============================================================================ */

typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t size : 1;
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;
    uint64_t available2 : 11;
    uint64_t nx : 1;
} __attribute__((packed)) x86_64_pml4e_t;

/* ============================================================================
 * PDPT Entry (Page Directory Pointer Table)
 * ============================================================================ */

typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t size : 1;
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;
    uint64_t available2 : 11;
    uint64_t nx : 1;
} __attribute__((packed)) x86_64_pdpte_t;

/* ============================================================================
 * PD Entry (Page Directory)
 * ============================================================================ */

typedef struct {
    uint64_t present : 1;
    uint64_t rw : 1;
    uint64_t user : 1;
    uint64_t pwt : 1;
    uint64_t pcd : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t size : 1;
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t frame : 40;
    uint64_t available2 : 11;
    uint64_t nx : 1;
} __attribute__((packed)) x86_64_pde_t;

/* ============================================================================
 * Macros for x86_64 page addressing
 * ============================================================================ */

#define X86_64_PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define X86_64_PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define X86_64_PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define X86_64_PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)
#define X86_64_PAGE_OFFSET(addr) ((addr) & 0xFFF)

/* ============================================================================
 * x86_64 constants
 * ============================================================================ */

#define X86_64_PAGE_SIZE 4096
#define X86_64_LARGE_PAGE_SIZE 2097152  /* 2MB */
#define X86_64_HUGE_PAGE_SIZE 1073741824 /* 1GB */

#endif /* _RODNIX_ARCH_X86_64_TYPES_H */

