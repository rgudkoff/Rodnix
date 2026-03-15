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
 * Process model (current):
 * - unix_proc_spawn(): create child process + immediately load program image.
 * - unix_proc_fork(): clone process with COW VM map.
 * - unix_fs_exec(): replace current process image in-place (pid is preserved).
 */
#define UNIX_PATH_MAX 256
#define UNIX_ARG_MAX 16
#define UNIX_ENV_MAX 32
#define UNIX_DIRENT_NAME_MAX 255

enum {
    UNIX_FD_KIND_NONE = 0,
    UNIX_FD_KIND_VFS = 1,
    UNIX_FD_KIND_PIPE_R = 2,
    UNIX_FD_KIND_PIPE_W = 3,
    UNIX_FD_KIND_SOCKET = 4
};

bool unix_user_range_ok(const void* ptr, size_t len);
int unix_copy_from_user(void* kdst, const void* usrc, size_t len);
int unix_copy_to_user(void* udst, const void* ksrc, size_t len);
int unix_copy_user_cstr(char* dst, size_t dst_size, const char* user_src);
int unix_resolve_path(const task_t* task, const char* in, char* out, size_t out_sz);
int unix_resolve_user_path(const char* user_src, char* out, size_t out_sz);

int unix_bind_stdio_to_console(task_t* task);
int unix_clone_fds_for_spawn(const task_t* parent, task_t* child);
void unix_apply_cloexec(task_t* task);
void unix_fd_release(task_t* task, int fd);

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
uint64_t unix_fs_openat(uint64_t dirfd, uint64_t user_path_ptr, uint64_t flags);
/* CT-007/CT-008 */
uint64_t unix_fs_close(uint64_t fd);
uint64_t unix_fs_dup(uint64_t oldfd);
uint64_t unix_fs_dup2(uint64_t oldfd, uint64_t newfd);
uint64_t unix_fs_dup3(uint64_t oldfd, uint64_t newfd, uint64_t flags);
uint64_t unix_fs_read(uint64_t fd, uint64_t user_buf_ptr, uint64_t len);
uint64_t unix_fs_write(uint64_t fd, uint64_t user_buf_ptr, uint64_t len);
uint64_t unix_fs_lseek(uint64_t fd, uint64_t off, uint64_t whence);
uint64_t unix_fs_truncate(uint64_t user_path_ptr, uint64_t size);
uint64_t unix_fs_ftruncate(uint64_t fd, uint64_t size);
uint64_t unix_fs_chdir(uint64_t user_path_ptr);
uint64_t unix_fs_getcwd(uint64_t user_buf_ptr, uint64_t size);
uint64_t unix_fs_mkdir(uint64_t user_path_ptr);
uint64_t unix_fs_unlink(uint64_t user_path_ptr);
uint64_t unix_fs_rmdir(uint64_t user_path_ptr);
uint64_t unix_fs_rename(uint64_t user_old_path_ptr, uint64_t user_new_path_ptr);
uint64_t unix_fs_ioctl(uint64_t fd, uint64_t request, uint64_t user_arg_ptr);
uint64_t unix_fs_stat(uint64_t user_path_ptr, uint64_t user_stat_ptr);
uint64_t unix_fs_fstat(uint64_t fd, uint64_t user_stat_ptr);
uint64_t unix_fs_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg);
uint64_t unix_fs_pipe(uint64_t user_pipefd_ptr);
uint64_t unix_fs_pipe2(uint64_t user_pipefd_ptr, uint64_t flags);
uint64_t unix_fs_socket(uint64_t domain, uint64_t type, uint64_t protocol);
uint64_t unix_fs_bind(uint64_t fd, uint64_t user_addr_ptr);
uint64_t unix_fs_connect(uint64_t fd, uint64_t user_addr_ptr);
uint64_t unix_fs_sendto(uint64_t fd,
                        uint64_t user_buf_ptr,
                        uint64_t len,
                        uint64_t flags,
                        uint64_t user_dst_addr_ptr,
                        uint64_t user_dst_len);
uint64_t unix_fs_recvfrom(uint64_t fd,
                          uint64_t user_buf_ptr,
                          uint64_t len,
                          uint64_t flags,
                          uint64_t user_src_addr_ptr,
                          uint64_t timeout_ms);
uint64_t unix_fs_poll(uint64_t user_fds_ptr, uint64_t nfds, int64_t timeout_ms);
uint64_t unix_fs_select(uint64_t nfds,
                        uint64_t user_readfds_ptr,
                        uint64_t user_writefds_ptr,
                        uint64_t user_exceptfds_ptr,
                        uint64_t user_timeout_ptr);
/* CT-003 target */
uint64_t unix_fs_exec(uint64_t user_path_ptr, uint64_t user_argv_ptr, uint64_t user_envp_ptr);
uint64_t unix_fs_readdir(uint64_t user_path_ptr, uint64_t user_entries_ptr, uint64_t user_len);

/* CT-005 lifecycle source event */
uint64_t unix_proc_exit(uint64_t status);
/* CT-001 */
uint64_t unix_proc_spawn(uint64_t user_path_ptr, uint64_t user_argv_ptr);
uint64_t unix_proc_fork(void);
uint64_t unix_proc_kill(uint64_t pid, uint64_t signum);
uint64_t unix_proc_sigaction(uint64_t signum, uint64_t user_act_ptr, uint64_t user_oldact_ptr);
uint64_t unix_proc_sigreturn(void);
uint64_t unix_proc_futex(uint64_t user_uaddr_ptr,
                         uint64_t op,
                         uint64_t val,
                         uint64_t user_timeout_ptr,
                         uint64_t user_uaddr2_ptr,
                         uint64_t val3);
void unix_proc_signal_checkpoint(void);
/* CT-004/CT-005/CT-006 */
uint64_t unix_proc_waitpid(uint64_t pid, uint64_t user_status_ptr);
uint64_t unix_time_nanosleep(uint64_t user_req_ptr, uint64_t user_rem_ptr);
void unix_proc_notify_waiters(uint64_t parent_task_id);
void unix_proc_close_fds(task_t* task);

#endif /* _RODNIX_UNIX_LAYER_H */
