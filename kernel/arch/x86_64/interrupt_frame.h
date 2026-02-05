/**
 * @file interrupt_frame.h
 * @brief x86_64 interrupt frame layout (matches isr_stubs.S push order)
 */

#ifndef _RODNIX_X86_64_INTERRUPT_FRAME_H
#define _RODNIX_X86_64_INTERRUPT_FRAME_H

#include <stdint.h>

typedef struct interrupt_frame {
    /* Segment registers saved by stubs */
    uint64_t gs;
    uint64_t fs;
    uint64_t es;
    uint64_t ds;

    /* General-purpose registers (high to low) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Interrupt metadata pushed by stubs */
    uint64_t int_no;
    uint64_t err_code;

    /* CPU-saved execution context (ring 0: rip/cs/rflags only) */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} interrupt_frame_t;

#endif /* _RODNIX_X86_64_INTERRUPT_FRAME_H */
