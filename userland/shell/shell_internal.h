#ifndef _RODNIX_USERLAND_SHELL_INTERNAL_H
#define _RODNIX_USERLAND_SHELL_INTERNAL_H

#include <stdint.h>
#include "syscall.h"
#include "posix_syscall.h"
#include "posix_sysnums.h"
#include "unistd.h"
#include "dirent.h"
#include "sysinfo.h"
#include "hwinfo.h"
#include "fabric_node.h"
#include "netif.h"

#define SYS_NOP 0
#define VFS_OPEN_READ 1
#define VFS_OPEN_WRITE 2
#define VFS_OPEN_CREATE 4
#define VFS_OPEN_TRUNC 8

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

#define SH_LINE_MAX 128
#define SH_ARG_MAX  16
#define SH_ANSI_CLEAR  "\x1b[2J"
#define SH_ANSI_BOTTOM "\x1b[999;1H"
#define SH_PATH_MAX 256
#define HOSTINFO_HW_MAX 64
#define HOSTINFO_FABRIC_MAX 128
#define HOSTINFO_NETIF_MAX 16
#define SH_PIPE_TMP "/tmp/.sh_pipe.tmp"

typedef struct {
    uint32_t abi_version;
    uint32_t size;
} rdnx_abi_header_t;

typedef struct {
    rdnx_abi_header_t hdr;
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[64];
    char machine[32];
} utsname_t;

extern char shell_cwd[SH_PATH_MAX];

long write_buf(const char* s, uint64_t len);
long write_str(const char* s);
void write_u64(uint64_t v);
void write_hex_u32(uint32_t v);
void write_mem_short(uint64_t bytes);
int str_eq(const char* a, const char* b);
int str_starts(const char* s, const char* p);
void sanitize_cmd_token(char* s);
void resolve_path(const char* in, char* out, int out_sz);
int read_line(char* out, int out_len);

int parse_line(char* line, char** argv, int max_args);
int line_has_meta(const char* s);

int cmd_exec_meta_line(char* line);
int cmd_run(int argc, char** argv, int verbose);
int cmd_cd(int argc, char** argv);
int cmd_autorun(int argc, char** argv);

void run_smoke(void);
void cmd_uname(void);
int cmd_hostinfo(void);
void cmd_hostname(void);
void cmd_ttytest(void);
int cmd_ls_builtin(const char* path);
void cmd_help(void);

#endif
