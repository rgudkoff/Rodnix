/**
 * @file giant.c
 * @brief The kernel-wide lock.
 */

#include "core/giant.h"

kmutex_t giant;

void giant_init(void)
{
    kmutex_init_recursive(&giant, "Giant");
}

uint32_t giant_drop(void)
{
    uint32_t depth = 0;
    while (kmutex_owned(&giant)) {
        kmutex_unlock(&giant);
        depth++;
    }
    return depth;
}

void giant_pickup(uint32_t depth)
{
    while (depth-- > 0) {
        kmutex_lock(&giant);
    }
}
