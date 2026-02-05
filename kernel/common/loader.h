#ifndef _RODNIX_COMMON_LOADER_H
#define _RODNIX_COMMON_LOADER_H

#include <stddef.h>

int loader_init(void);
int loader_load_image(const void* image, size_t size);
int loader_enter_user_stub(void);

#endif /* _RODNIX_COMMON_LOADER_H */
