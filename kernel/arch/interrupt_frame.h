/**
 * @file arch/interrupt_frame.h
 * @brief Common entry point for architecture interrupt frames.
 */

#ifndef _RODNIX_ARCH_INTERRUPT_FRAME_H
#define _RODNIX_ARCH_INTERRUPT_FRAME_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/interrupt_frame.h"
#else
#error "Architecture interrupt frame is not wired for this target yet"
#endif

#endif /* _RODNIX_ARCH_INTERRUPT_FRAME_H */
