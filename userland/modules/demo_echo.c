#include "../../include/kmod_abi.h"

__attribute__((used, section(".rodnix_mod")))
const kmod_image_header_t g_demo_kmod = {
    .magic = KMOD_IMAGE_MAGIC,
    .name = "demo.echo",
    .kind = "misc",
    .version = "0.2",
    .flags = 0,
    .image_size = sizeof(kmod_image_header_t),
    .reserved0 = 0,
    .reserved1 = 0
};

int rodnix_kmod_init(void)
{
    return 0;
}

void rodnix_kmod_fini(void)
{
}
