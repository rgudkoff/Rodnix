/**
 * @file kernel.h
 * @brief Main kernel header
 * 
 * Includes all necessary headers for kernel development.
 */

#ifndef _RODNIX_KERNEL_H
#define _RODNIX_KERNEL_H

/* Basic types */
#include "types.h"

/* Core abstractions - config.h must come before arch_types.h */
#include "../kernel/core/config.h"
#include "../kernel/core/arch_types.h"
#include "../kernel/core/interrupts.h"
#include "../kernel/core/memory.h"
#include "../kernel/core/cpu.h"
#include "../kernel/core/boot.h"
#include "../kernel/core/task.h"

/* Common components */
#include "../kernel/common/scheduler.h"
#include "../kernel/common/ipc.h"
#include "../kernel/common/device.h"

#endif /* _RODNIX_KERNEL_H */

