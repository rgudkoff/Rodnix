#ifndef _RODNIX_UNIX_LAYER_H
#define _RODNIX_UNIX_LAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../core/task.h"

/*
 * Unix compatibility layer:
 * kernel mechanisms -> unix semantics -> posix syscall ABI adapter.
 *
 * Process model (current, explicit):
 * - unix_proc_spawn(): create child process + immediately load program image.
 *   This is a spawn-as-primitive model (no fork() yet).
 * - unix_fs_exec(): replace current process image in-place (pid is preserved).
 *
 * Planned evolution:
 * - fork()+exec() can be introduced later without breaking POSIX ABI entrypoints.
 */
#define UNIX_PATH_MAX 256
#define UNIX_ARG_MAX 16
#define UNIX_DIRENT_NAME_MAX 255

bool unix_user_range_ok(const void* ptr, size_t len);
int unix_copy_user_cstr(char* dst, size_t dst_size, const char* user_src);

int unix_bind_stdio_to_console(task_t* task);
int unix_clone_fds_for_spawn(const task_t* parent, task_t* child);
void unix_apply_cloexec(task_t* task);

typedef struct {
    uint64_t d_fileno;
    uint16_t d_reclen;
    uint8_t d_type;
    uint8_t d_namlen;
    char d_name[UNIX_DIRENT_NAME_MAX + 1];
} unix_dirent_u_t;

typedef struct {
    uint32_t st_mode;
    int64_t st_size;
} unix_stat_u_t;

uint64_t unix_fs_open(uint64_t user_path_ptr, uint64_t flags);
/* CT-007/CT-008 */
uint64_t unix_fs_close(uint64_t fd);
uint64_t unix_fs_read(uint64_t fd, uint64_t user_buf_ptr, uint64_t len);
uint64_t unix_fs_write(uint64_t fd, uint64_t user_buf_ptr, uint64_t len);
uint64_t unix_fs_lseek(uint64_t fd, uint64_t off, uint64_t whence);
uint64_t unix_fs_stat(uint64_t user_path_ptr, uint64_t user_stat_ptr);
uint64_t unix_fs_fstat(uint64_t fd, uint64_t user_stat_ptr);
uint64_t unix_fs_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg);
/* CT-003 target */
uint64_t unix_fs_exec(uint64_t user_path_ptr);
uint64_t unix_fs_readdir(uint64_t user_path_ptr, uint64_t user_entries_ptr, uint64_t user_len);

/* CT-005 lifecycle source event */
uint64_t unix_proc_exit(uint64_t status);
/* CT-001 */
uint64_t unix_proc_spawn(uint64_t user_path_ptr, uint64_t user_argv_ptr);
uint64_t unix_proc_fork(void);
/* CT-004/CT-005/CT-006 */
uint64_t unix_proc_waitpid(uint64_t pid, uint64_t user_status_ptr);
void unix_proc_notify_waiters(uint64_t parent_task_id);

#endif /* _RODNIX_UNIX_LAYER_H */
