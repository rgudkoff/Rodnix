/**
 * @file spin.c
 * @brief Simple spinlock implementation for Fabric
 */

#include "spin.h"

void spinlock_init(spinlock_t* lock)
{
    if (!lock) {
        return;
    }
    lock->locked = 0;
    __asm__ volatile ("" ::: "memory");
}

void spinlock_lock(spinlock_t* lock)
{
    if (!lock) {
        return;
    }
    
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile ("pause");
    }
    __asm__ volatile ("" ::: "memory");
}

void spinlock_unlock(spinlock_t* lock)
{
    if (!lock) {
        return;
    }
    
    __asm__ volatile ("" ::: "memory");
    __sync_lock_release(&lock->locked);
}

bool spinlock_trylock(spinlock_t* lock)
{
    if (!lock) {
        return false;
    }
    
    return __sync_lock_test_and_set(&lock->locked, 1) == 0;
}

