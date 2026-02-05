/**
 * @file loader.c
 * @brief Minimal userland loader placeholder
 */

#include "loader.h"
#include "../arch/x86_64/usermode.h"
#include "../core/task.h"
#include "../../include/console.h"
#include "../../include/error.h"

int loader_init(void)
{
    return 0;
}

int loader_load_image(const void* image, size_t size)
{
    (void)image;
    (void)size;
    kputs("[LOADER] Not implemented\n");
    return RDNX_E_UNSUPPORTED;
}

int loader_enter_user_stub(void)
{
    kputs("[LOADER] enter_user_stub\n");
    void* entry = NULL;
    void* user_stack = NULL;
    uint64_t rsp0 = 0;
    if (usermode_prepare_stub(&entry, &user_stack, &rsp0) != 0) {
        kputs("[LOADER] prepare_stub failed\n");
        return RDNX_E_GENERIC;
    }
    thread_t* cur = thread_get_current();
    if (cur && cur->stack) {
        rsp0 = (uint64_t)(uintptr_t)cur->stack + cur->stack_size - 16;
    }
    if (!rsp0) {
        kputs("[LOADER] rsp0 missing\n");
        return RDNX_E_INVALID;
    }
    usermode_enter(entry, user_stack, rsp0);
    return 0;
}
