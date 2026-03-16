#ifndef _RODNIX_COMMON_LOADER_H
#define _RODNIX_COMMON_LOADER_H

#include <stddef.h>
#include <stdint.h>

#define LOADER_USER_STACK_PAGES 4
#define LOADER_MAX_SEGMENTS 16

typedef struct {
    uint64_t start;
    uint64_t end;
    uint32_t prot;
} loader_segment_t;

typedef struct {
    uint64_t pml4_phys;
    uint64_t entry;
    uint64_t user_stack;
    uint64_t stack_bottom;
    uint64_t stack_phys[LOADER_USER_STACK_PAGES];
    uint32_t seg_count;
    loader_segment_t segs[LOADER_MAX_SEGMENTS];
    uint64_t brk_base;
    uint8_t abi;
} loader_image_t;

typedef int (*loader_pre_exec_commit_fn)(void* ctx);

int loader_init(void);
int loader_load_image(const void* image, size_t size);
int loader_enter_user_stub(void);
int loader_exec(const char* path);
int loader_execve(const char* path, int argc, const char* const argv[], const char* const envp[]);
int loader_execve_ex(const char* path,
                     int argc,
                     const char* const argv[],
                     const char* const envp[],
                     loader_pre_exec_commit_fn pre_commit,
                     void* pre_commit_ctx);

#endif /* _RODNIX_COMMON_LOADER_H */
