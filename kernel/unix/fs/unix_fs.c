#include "../unix_layer.h"
#include "../../fs/vfs.h"
#include "../../../include/common.h"
#include "../../../include/error.h"

#define UNIX_DT_UNKNOWN 0
#define UNIX_DT_DIR 4
#define UNIX_DT_REG 8

typedef struct {
    unix_dirent_u_t* out;
    size_t cap_bytes;
    size_t used_bytes;
    uint64_t next_ino;
} unix_readdir_ctx_t;

static void unix_readdir_cb(const vfs_node_t* node, void* ctx)
{
    unix_readdir_ctx_t* c = (unix_readdir_ctx_t*)ctx;
    if (!node || !c || !c->out) {
        return;
    }
    if (c->used_bytes + sizeof(unix_dirent_u_t) > c->cap_bytes) {
        return;
    }

    unix_dirent_u_t* de = (unix_dirent_u_t*)((uint8_t*)c->out + c->used_bytes);
    memset(de, 0, sizeof(*de));
    de->d_fileno = c->next_ino++;
    de->d_reclen = (uint16_t)sizeof(*de);
    de->d_type = (node->type == VFS_NODE_DIR) ? UNIX_DT_DIR :
                 (node->type == VFS_NODE_FILE) ? UNIX_DT_REG : UNIX_DT_UNKNOWN;

    size_t nlen = strlen(node->name);
    if (nlen > UNIX_DIRENT_NAME_MAX) {
        nlen = UNIX_DIRENT_NAME_MAX;
    }
    memcpy(de->d_name, node->name, nlen);
    de->d_name[nlen] = '\0';
    de->d_namlen = (uint8_t)nlen;
    c->used_bytes += sizeof(*de);
}

uint64_t unix_fs_readdir(uint64_t user_path_ptr, uint64_t user_entries_ptr, uint64_t user_len)
{
    void* user_buf = (void*)(uintptr_t)user_entries_ptr;
    size_t n = (size_t)user_len;
    char path_buf[UNIX_PATH_MAX];

    if (n < sizeof(unix_dirent_u_t)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (!unix_user_range_ok(user_buf, n)) {
        return (uint64_t)RDNX_E_INVALID;
    }
    if (unix_resolve_user_path((const char*)(uintptr_t)user_path_ptr, path_buf, sizeof(path_buf)) != RDNX_OK) {
        return (uint64_t)RDNX_E_INVALID;
    }

    unix_readdir_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.out = (unix_dirent_u_t*)user_buf;
    ctx.cap_bytes = n;
    ctx.next_ino = 1;

    int rc = vfs_list_dir(path_buf, unix_readdir_cb, &ctx);
    if (rc != RDNX_OK) {
        return (uint64_t)rc;
    }
    return (uint64_t)ctx.used_bytes;
}
