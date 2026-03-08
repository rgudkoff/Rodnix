#ifndef _RODNIX_COMMON_LOADER_H
#define _RODNIX_COMMON_LOADER_H

#include <stddef.h>
#include <stdint.h>

#define LOADER_USER_STACK_PAGES 4

typedef struct {
    uint64_t pml4_phys;
    uint64_t entry;
    uint64_t user_stack;
    uint64_t stack_bottom;
    uint64_t stack_phys[LOADER_USER_STACK_PAGES];
} loader_image_t;

int loader_init(void);
int loader_load_image(const void* image, size_t size);
int loader_enter_user_stub(void);
int loader_exec(const char* path);
int loader_execve(const char* path, int argc, const char* const argv[]);

#endif /* _RODNIX_COMMON_LOADER_H */
