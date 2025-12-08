#ifndef _RODNIX_PIT_H
#define _RODNIX_PIT_H

#include "types.h"

void pit_init(uint32_t freq_hz);
uint64_t pit_ticks(void);

#endif

