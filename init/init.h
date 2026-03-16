/**
 * @file init.h
 * @brief Kernel bootstrap orchestration helpers
 */

#ifndef _RODNIX_KERNEL_INIT_H
#define _RODNIX_KERNEL_INIT_H

const char* kernel_timer_source_name(void);
void kernel_run_bootstrap_sysinit(void);
void kernel_enable_runtime_interrupts(void);
void kernel_enter_runtime(void) __attribute__((noreturn));

#endif /* _RODNIX_KERNEL_INIT_H */
