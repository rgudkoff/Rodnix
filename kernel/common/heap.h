/**
 * @file heap.h
 * @brief Simple kernel heap interface
 */

#ifndef _RODNIX_COMMON_HEAP_H
#define _RODNIX_COMMON_HEAP_H

#include <stddef.h>
#include <stdint.h>

int heap_init(size_t initial_pages);
void* kmalloc(size_t size);
void kfree(void* ptr);
void* kcalloc(size_t count, size_t size);
void* krealloc(void* ptr, size_t new_size);

#endif /* _RODNIX_COMMON_HEAP_H */
