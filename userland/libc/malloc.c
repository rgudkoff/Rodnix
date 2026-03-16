#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* g_heap_head = 0;
static heap_block_t* g_heap_tail = 0;

static size_t align16(size_t n)
{
    return (n + 15u) & ~(size_t)15u;
}

static heap_block_t* block_from_ptr(void* ptr)
{
    if (!ptr) {
        return 0;
    }
    return (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
}

static void split_block(heap_block_t* block, size_t size)
{
    if (!block || block->size <= size + sizeof(heap_block_t) + 16u) {
        return;
    }
    heap_block_t* new_block = (heap_block_t*)((uint8_t*)block + sizeof(heap_block_t) + size);
    new_block->size = block->size - size - sizeof(heap_block_t);
    new_block->free = 1;
    new_block->next = block->next;
    block->size = size;
    block->next = new_block;
    if (g_heap_tail == block) {
        g_heap_tail = new_block;
    }
}

static void coalesce(void)
{
    heap_block_t* cur = g_heap_head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(heap_block_t) + cur->next->size;
            cur->next = cur->next->next;
            if (!cur->next) {
                g_heap_tail = cur;
            }
            continue;
        }
        cur = cur->next;
    }
}

void* malloc(size_t size)
{
    heap_block_t* cur;
    heap_block_t* block;
    size_t need;
    void* mem;

    if (size == 0) {
        return 0;
    }
    need = align16(size);

    for (cur = g_heap_head; cur; cur = cur->next) {
        if (cur->free && cur->size >= need) {
            cur->free = 0;
            split_block(cur, need);
            return (uint8_t*)cur + sizeof(heap_block_t);
        }
    }

    mem = sbrk((intptr_t)(sizeof(heap_block_t) + need));
    if (mem == (void*)-1) {
        return 0;
    }

    block = (heap_block_t*)mem;
    block->size = need;
    block->free = 0;
    block->next = 0;
    if (!g_heap_head) {
        g_heap_head = block;
    } else if (g_heap_tail) {
        g_heap_tail->next = block;
    }
    g_heap_tail = block;
    return (uint8_t*)block + sizeof(heap_block_t);
}

void free(void* ptr)
{
    heap_block_t* block = block_from_ptr(ptr);
    if (!block) {
        return;
    }
    block->free = 1;
    coalesce();
}

void* realloc(void* ptr, size_t size)
{
    heap_block_t* block;
    void* out;
    size_t copy_n;

    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return 0;
    }

    block = block_from_ptr(ptr);
    if (block->size >= size) {
        split_block(block, align16(size));
        return ptr;
    }

    out = malloc(size);
    if (!out) {
        return 0;
    }
    copy_n = block->size < size ? block->size : size;
    memcpy(out, ptr, copy_n);
    free(ptr);
    return out;
}

void* calloc(size_t nmemb, size_t size)
{
    size_t total;
    void* out;
    if (nmemb == 0 || size == 0) {
        return malloc(0);
    }
    if (nmemb > ((size_t)-1 / size)) {
        return 0;
    }
    total = nmemb * size;
    out = malloc(total);
    if (!out) {
        return 0;
    }
    memset(out, 0, total);
    return out;
}
