/*
 * contract_heap.c
 * Heap contract probe for libc-lite allocator.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FD_STDOUT 1

static void put_str(const char* s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    (void)write(FD_STDOUT, s, n);
}

int main(void)
{
    uint8_t* a = (uint8_t*)malloc(128);
    uint8_t* b = (uint8_t*)calloc(64, 4);
    if (!a || !b) {
        put_str("[CT] HEAP FAIL alloc/calloc failed\n");
        return 10;
    }

    for (size_t i = 0; i < 128; i++) {
        a[i] = (uint8_t)(i ^ 0x5Au);
    }
    for (size_t i = 0; i < 256; i++) {
        if (b[i] != 0) {
            put_str("[CT] HEAP FAIL calloc not zeroed\n");
            free(a);
            free(b);
            return 11;
        }
    }

    uint8_t* a2 = (uint8_t*)realloc(a, 512);
    if (!a2) {
        put_str("[CT] HEAP FAIL realloc failed\n");
        free(a);
        free(b);
        return 12;
    }
    for (size_t i = 0; i < 128; i++) {
        if (a2[i] != (uint8_t)(i ^ 0x5Au)) {
            put_str("[CT] HEAP FAIL realloc data mismatch\n");
            free(a2);
            free(b);
            return 13;
        }
    }

    memset(a2 + 128, 0xA5, 384);
    free(a2);
    free(b);

    put_str("[CT] HEAP PASS malloc/free/realloc/calloc\n");
    return 0;
}
