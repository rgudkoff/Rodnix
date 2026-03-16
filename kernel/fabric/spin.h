/**
 * @file spin.h
 * @brief Simple spinlock implementation for Fabric
 */

#ifndef _RODNIX_FABRIC_SPIN_H
#define _RODNIX_FABRIC_SPIN_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

void spinlock_init(spinlock_t* lock);
void spinlock_lock(spinlock_t* lock);
void spinlock_unlock(spinlock_t* lock);
bool spinlock_trylock(spinlock_t* lock);

#endif /* _RODNIX_FABRIC_SPIN_H */

