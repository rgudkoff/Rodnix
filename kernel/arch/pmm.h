/**
 * @file arch/pmm.h
 * @brief Common entry point for architecture PMM helpers.
 */

#ifndef _RODNIX_ARCH_PMM_H
#define _RODNIX_ARCH_PMM_H

#if defined(__x86_64__) || defined(_M_X64)
#include "x86_64/pmm.h"
#else
#error "Architecture PMM interface is not wired for this target yet"
#endif

/* Датчик давления: сколько страниц свободно/всего. Читается без замка —
 * число устаревает в момент чтения, и точность тут не нужна. */
uint64_t pmm_free_pages_count(void);
uint64_t pmm_total_pages_count(void);

#endif /* _RODNIX_ARCH_PMM_H */
