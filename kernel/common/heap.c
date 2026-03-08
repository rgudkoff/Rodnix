/**
 * @file heap.c
 * @brief Simple kernel heap allocator
 */

#include "heap.h"
#include "../../include/common.h"
#include "../../include/debug.h"
#include "../../include/error.h"
#include "../core/memory.h"
#include "../core/config.h"
#include "../core/interrupts.h"

typedef struct heap_block {
    size_t size;
    bool free;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

static heap_block_t* heap_head = NULL;
static heap_block_t* heap_tail = NULL;

static inline irql_t heap_lock(void)
{
    return set_irql(IRQL_HIGH);
}

static inline void heap_unlock(irql_t old)
{
    (void)set_irql(old);
}

static bool heap_block_in_list(const heap_block_t* needle)
{
    if (!needle) {
        return false;
    }
    uint32_t guard = 0;
    for (heap_block_t* cur = heap_head; cur; cur = cur->next) {
        if (cur == needle) {
            return true;
        }
        guard++;
        if (guard > 1000000u) {
            break;
        }
    }
    return false;
}

static size_t heap_align(size_t size)
{
    const size_t align = 16;
    return ALIGN_UP(size, align);
}

static void heap_insert_block(heap_block_t* block)
{
    block->next = NULL;
    block->prev = heap_tail;
    if (heap_tail) {
        heap_tail->next = block;
    } else {
        heap_head = block;
    }
    heap_tail = block;
}

static void heap_merge_if_possible(heap_block_t* block)
{
    if (!block) {
        return;
    }

    if (block->next && block->next->free) {
        uint8_t* end = (uint8_t*)block + sizeof(heap_block_t) + block->size;
        if (end == (uint8_t*)block->next) {
            heap_block_t* next = block->next;
            block->size += sizeof(heap_block_t) + next->size;
            block->next = next->next;
            if (next->next) {
                next->next->prev = block;
            } else {
                heap_tail = block;
            }
        }
    }

    if (block->prev && block->prev->free) {
        uint8_t* end = (uint8_t*)block->prev + sizeof(heap_block_t) + block->prev->size;
        if (end == (uint8_t*)block) {
            heap_block_t* prev = block->prev;
            prev->size += sizeof(heap_block_t) + block->size;
            prev->next = block->next;
            if (block->next) {
                block->next->prev = prev;
            } else {
                heap_tail = prev;
            }
        }
    }
}

static heap_block_t* heap_find_fit(size_t size)
{
    for (heap_block_t* cur = heap_head; cur; cur = cur->next) {
        if (cur->free && cur->size >= size) {
            return cur;
        }
    }
    return NULL;
}

static void heap_split_block(heap_block_t* block, size_t size)
{
    size_t aligned = heap_align(size);
    if (block->size <= aligned + sizeof(heap_block_t) + 16) {
        return;
    }

    uint8_t* next_addr = (uint8_t*)block + sizeof(heap_block_t) + aligned;
    heap_block_t* next = (heap_block_t*)next_addr;
    next->size = block->size - aligned - sizeof(heap_block_t);
    next->free = true;
    next->prev = block;
    next->next = block->next;
    if (block->next) {
        block->next->prev = next;
    } else {
        heap_tail = next;
    }
    block->next = next;
    block->size = aligned;
}

static heap_block_t* heap_grow(size_t min_size)
{
    size_t total = heap_align(min_size) + sizeof(heap_block_t);
    size_t pages = ALIGN_UP(total, PAGE_SIZE) / PAGE_SIZE;
    if (pages == 0) {
        pages = 1;
    }

    void* mem = vmm_alloc_pages((uint32_t)pages, PAGE_FLAG_WRITABLE);
    if (!mem) {
        TRACE_EVENT("oom: heap_grow");
        memory_oom_inc_heap();
        PANIC("OOM: heap_grow pages=%u", (unsigned)pages);
    }

    heap_block_t* block = (heap_block_t*)mem;
    block->size = pages * PAGE_SIZE - sizeof(heap_block_t);
    block->free = true;
    block->next = NULL;
    block->prev = NULL;
    heap_insert_block(block);

    if (block->prev && block->prev->free) {
        uint8_t* prev_end = (uint8_t*)block->prev + sizeof(heap_block_t) + block->prev->size;
        if (prev_end == (uint8_t*)block) {
            heap_merge_if_possible(block->prev);
            /* Newly inserted block is absorbed into prev. */
            return block->prev;
        }
    }

    return block;
}

int heap_init(size_t initial_pages)
{
    if (heap_head) {
        return RDNX_OK;
    }

    if (initial_pages == 0) {
        initial_pages = 16;
    }

    void* mem = vmm_alloc_pages((uint32_t)initial_pages, PAGE_FLAG_WRITABLE);
    if (!mem) {
        TRACE_EVENT("oom: heap_init");
        memory_oom_inc_heap();
        PANIC("OOM: heap_init pages=%u", (unsigned)initial_pages);
    }

    heap_block_t* block = (heap_block_t*)mem;
    block->size = initial_pages * PAGE_SIZE - sizeof(heap_block_t);
    block->free = true;
    block->next = NULL;
    block->prev = NULL;
    heap_head = block;
    heap_tail = block;

    return RDNX_OK;
}

void* kmalloc(size_t size)
{
    irql_t old = heap_lock();
    if (size == 0) {
        heap_unlock(old);
        return NULL;
    }

    size_t aligned = heap_align(size);
    heap_block_t* block = heap_find_fit(aligned);
    if (!block) {
        block = heap_grow(aligned);
        if (!block) {
            heap_unlock(old);
            return NULL;
        }
    }

    heap_split_block(block, aligned);
    block->free = false;
    void* ptr = (uint8_t*)block + sizeof(heap_block_t);
    heap_unlock(old);
    return ptr;
}

void kfree(void* ptr)
{
    irql_t old = heap_lock();
    if (!ptr) {
        heap_unlock(old);
        return;
    }

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (!heap_block_in_list(block)) {
        heap_unlock(old);
        PANIC("kfree: invalid pointer %p block=%p ra=%p", ptr, block, __builtin_return_address(0));
    }
    if (block->free) {
        heap_unlock(old);
        PANIC("kfree: double free ptr=%p block=%p ra=%p", ptr, block, __builtin_return_address(0));
    }
    block->free = true;
    heap_merge_if_possible(block);
    heap_unlock(old);
}

void* kcalloc(size_t count, size_t size)
{
    if (count == 0 || size == 0) {
        return NULL;
    }

    size_t total = count * size;
    if (total / size != count) {
        return NULL;
    }

    void* mem = kmalloc(total);
    if (!mem) {
        return NULL;
    }
    memset(mem, 0, total);
    return mem;
}

void* krealloc(void* ptr, size_t new_size)
{
    if (!ptr) {
        return kmalloc(new_size);
    }
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->size >= new_size) {
        heap_split_block(block, new_size);
        return ptr;
    }

    void* new_mem = kmalloc(new_size);
    if (!new_mem) {
        return NULL;
    }
    memcpy(new_mem, ptr, block->size);
    kfree(ptr);
    return new_mem;
}
