SHELL := /bin/bash
.DEFAULT_GOAL := all

ARCH ?= x86_64
TOOLCHAIN ?= gcc

ifeq ($(ARCH),x86_64)
TARGET_TRIPLE := x86_64-elf
ARCH_CFLAGS = -m64
ARCH_ASFLAGS = -f elf64
else ifeq ($(ARCH),arm64)
TARGET_TRIPLE := aarch64-elf
ARCH_CFLAGS =
ARCH_ASFLAGS =
else ifeq ($(ARCH),riscv64)
TARGET_TRIPLE := riscv64-elf
ARCH_CFLAGS =
ARCH_ASFLAGS =
else
$(error Unsupported ARCH '$(ARCH)')
endif

ifeq ($(TOOLCHAIN),gcc)
CROSS_COMPILE ?= $(TARGET_TRIPLE)-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
COMMON_TC_CFLAGS =
else ifeq ($(TOOLCHAIN),clang)
CC = clang
CXX = clang++
LD = ld.lld
COMMON_TC_CFLAGS = --target=$(TARGET_TRIPLE)
else
$(error Unsupported TOOLCHAIN '$(TOOLCHAIN)')
endif

ifeq ($(ARCH),x86_64)
AS = nasm
else
AS = $(CC)
endif

CFLAGS = $(COMMON_TC_CFLAGS) \
         $(ARCH_CFLAGS) \
         -ffreestanding \
         -fno-builtin \
         -fno-stack-protector \
         -nostdlib \
         -O2 \
         -g \
         -Wall \
         -Wextra \
         -I./include

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -std=c++17 -MMD -MP

ASFLAGS = $(ARCH_ASFLAGS)
LDFLAGS = -T link.ld

BUILD_ROOT = build
BUILD_DIR = $(BUILD_ROOT)/$(ARCH)
ROOTFS_DIR = rootfs
BIN_DIR = $(ROOTFS_DIR)/bin

INIT_SRCS = init/init.c
SH_SRCS = shell/main.c shell/io.c shell/parser.c shell/exec.c shell/builtins.c
ECHO_SRCS = bin/echo.c
LS_SRCS = bin/ls.c
CAT_SRCS = bin/cat.c
TRUE_SRCS = bin/true.c
IFCONFIG_SRCS = bin/ifconfig.c
PING_SRCS = bin/ping.c
HWLIST_SRCS = bin/hwlist.c
DEVREG_SRCS = bin/devreg.c
FABRICLS_SRCS = bin/fabricls.c
FABRICEVENTS_SRCS = bin/fabricevents.c
FABRICNETCHECK_SRCS = bin/fabricnetcheck.c
HOSTINFO_SRCS = bin/hostinfo.c
CPUINFO_SRCS = bin/cpuinfo.c
DISKINFO_SRCS = bin/diskinfo.c
KMODCTL_SRCS = bin/kmodctl.c
SLEEP_SRCS = bin/sleep.c
SIGTEST_SRCS = bin/sigtest.c
STTY_SRCS = bin/stty.c
TIMECHECK_SRCS = bin/timecheck.c
DATE_SRCS = bin/date.c
HWCLOCK_SRCS = bin/hwclock.c
SYSCALLTEST_SRCS = bin/syscalltest.c
TTYREADTEST_SRCS = bin/ttyreadtest.c
SCSTAT_SRCS = bin/scstat.c
STDIO_SMOKE_SRCS = bin/stdio_smoke.c
POLLTEST_SRCS = bin/polltest.c
SELECTTEST_SRCS = bin/selecttest.c
FUTEXTEST_SRCS = bin/futextest.c
PIPETEST_SRCS = bin/pipetest.c
UDPTEST_SRCS = bin/udptest.c
TCPTEST_SRCS = bin/tcptest.c
FSAPITEST_SRCS = bin/fsapitest.c
FORKTEST_SRCS = bin/forktest.c
EXECVETEST_SRCS = bin/execvetest.c
CONTRACT_FD_SRCS = bin/contract_fd.c
CONTRACT_FD_INHERIT_SRCS = bin/contract_fd_inherit.c
CONTRACT_SPAWN_WAIT_SRCS = bin/contract_spawn_wait.c
CONTRACT_EXEC_PROBE_SRCS = bin/contract_exec_probe.c
CONTRACT_EXEC_AFTER_SRCS = bin/contract_exec_after.c
CONTRACT_HEAP_SRCS = bin/contract_heap.c
CONTRACT_DIRENT_SRCS = bin/contract_dirent.c
CONTRACT_FSIO_SRCS = bin/contract_fsio.c
CONTRACT_WAIT_NONCHILD_SRCS = bin/contract_wait_nonchild.c
CP_SRCS   = bin/cp.c
MV_SRCS   = bin/mv.c
RM_SRCS   = bin/rm.c
GREP_SRCS   = bin/grep.c
FIND_SRCS   = bin/find.c
LOGIN_SRCS  = bin/login.c
PASSWD_SRCS = bin/passwd.c
ID_SRCS     = bin/id.c
GROUPS_SRCS = bin/groups.c
WHOAMI_SRCS = bin/whoami.c
PS_SRCS    = bin/ps.c
TOP_SRCS   = bin/top.c
VI_SRCS     = bin/vi.c
PTHREAD_TEST_SRCS_CPP = bin/pthread_test.cpp

CRT0_OBJ = $(BUILD_DIR)/crt0.o
LIBC_SRCS = libc/errno.c libc/string.c libc/ctype.c libc/stdlib.c libc/stdio.c libc/malloc.c libc/dirent.c libc/inet.c libc/libgen.c libc/math.c libc/posix.c libc/getopt.c libc/sha256.c libc/grp.c libc/pwd.c libc/pthread.c

ifeq ($(ARCH),x86_64)
LIBC_OBJS = $(addprefix $(BUILD_DIR)/, $(LIBC_SRCS:.c=.o)) $(BUILD_DIR)/libc/setjmp.o $(BUILD_DIR)/libc/pthread_clone.o
else
LIBC_OBJS = $(addprefix $(BUILD_DIR)/, $(LIBC_SRCS:.c=.o))
endif

ifeq ($(ARCH),x86_64)
$(BUILD_DIR)/libc/setjmp.o: libc/setjmp.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/libc/pthread_clone.o: libc/pthread_clone.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@
endif

INIT_OBJS = $(addprefix $(BUILD_DIR)/, $(INIT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SH_OBJS = $(addprefix $(BUILD_DIR)/, $(SH_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
ECHO_OBJS = $(addprefix $(BUILD_DIR)/, $(ECHO_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
LS_OBJS = $(addprefix $(BUILD_DIR)/, $(LS_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CAT_OBJS = $(addprefix $(BUILD_DIR)/, $(CAT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
TRUE_OBJS = $(addprefix $(BUILD_DIR)/, $(TRUE_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
IFCONFIG_OBJS = $(addprefix $(BUILD_DIR)/, $(IFCONFIG_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
PING_OBJS = $(addprefix $(BUILD_DIR)/, $(PING_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
HWLIST_OBJS = $(addprefix $(BUILD_DIR)/, $(HWLIST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
DEVREG_OBJS = $(addprefix $(BUILD_DIR)/, $(DEVREG_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FABRICLS_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICLS_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FABRICEVENTS_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICEVENTS_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FABRICNETCHECK_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICNETCHECK_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
HOSTINFO_OBJS = $(addprefix $(BUILD_DIR)/, $(HOSTINFO_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CPUINFO_OBJS = $(addprefix $(BUILD_DIR)/, $(CPUINFO_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
DISKINFO_OBJS = $(addprefix $(BUILD_DIR)/, $(DISKINFO_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
KMODCTL_OBJS = $(addprefix $(BUILD_DIR)/, $(KMODCTL_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SLEEP_OBJS = $(addprefix $(BUILD_DIR)/, $(SLEEP_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SIGTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(SIGTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
STTY_OBJS = $(addprefix $(BUILD_DIR)/, $(STTY_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
TIMECHECK_OBJS = $(addprefix $(BUILD_DIR)/, $(TIMECHECK_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
DATE_OBJS = $(addprefix $(BUILD_DIR)/, $(DATE_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
HWCLOCK_OBJS = $(addprefix $(BUILD_DIR)/, $(HWCLOCK_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SYSCALLTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(SYSCALLTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
TTYREADTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(TTYREADTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SCSTAT_OBJS = $(addprefix $(BUILD_DIR)/, $(SCSTAT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
STDIO_SMOKE_OBJS = $(addprefix $(BUILD_DIR)/, $(STDIO_SMOKE_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
POLLTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(POLLTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SELECTTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(SELECTTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FUTEXTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(FUTEXTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
PIPETEST_OBJS = $(addprefix $(BUILD_DIR)/, $(PIPETEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
UDPTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(UDPTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
TCPTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(TCPTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FSAPITEST_OBJS = $(addprefix $(BUILD_DIR)/, $(FSAPITEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FORKTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(FORKTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
EXECVETEST_OBJS = $(addprefix $(BUILD_DIR)/, $(EXECVETEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_FD_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_FD_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_FD_INHERIT_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_FD_INHERIT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_SPAWN_WAIT_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_SPAWN_WAIT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_EXEC_PROBE_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_EXEC_PROBE_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_EXEC_AFTER_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_EXEC_AFTER_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_HEAP_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_HEAP_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_DIRENT_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_DIRENT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_FSIO_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_FSIO_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CONTRACT_WAIT_NONCHILD_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_WAIT_NONCHILD_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CP_OBJS   = $(addprefix $(BUILD_DIR)/, $(CP_SRCS:.c=.o))   $(CRT0_OBJ) $(LIBC_OBJS)
MV_OBJS   = $(addprefix $(BUILD_DIR)/, $(MV_SRCS:.c=.o))   $(CRT0_OBJ) $(LIBC_OBJS)
RM_OBJS   = $(addprefix $(BUILD_DIR)/, $(RM_SRCS:.c=.o))   $(CRT0_OBJ) $(LIBC_OBJS)
GREP_OBJS   = $(addprefix $(BUILD_DIR)/, $(GREP_SRCS:.c=.o))   $(CRT0_OBJ) $(LIBC_OBJS)
FIND_OBJS   = $(addprefix $(BUILD_DIR)/, $(FIND_SRCS:.c=.o))   $(CRT0_OBJ) $(LIBC_OBJS)
LOGIN_OBJS  = $(addprefix $(BUILD_DIR)/, $(LOGIN_SRCS:.c=.o))  $(CRT0_OBJ) $(LIBC_OBJS)
PASSWD_OBJS = $(addprefix $(BUILD_DIR)/, $(PASSWD_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
ID_OBJS     = $(addprefix $(BUILD_DIR)/, $(ID_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
GROUPS_OBJS = $(addprefix $(BUILD_DIR)/, $(GROUPS_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
WHOAMI_OBJS = $(addprefix $(BUILD_DIR)/, $(WHOAMI_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
PS_OBJS    = $(addprefix $(BUILD_DIR)/, $(PS_SRCS:.c=.o))    $(CRT0_OBJ) $(LIBC_OBJS)
TOP_OBJS   = $(addprefix $(BUILD_DIR)/, $(TOP_SRCS:.c=.o))   $(CRT0_OBJ) $(LIBC_OBJS)
VI_OBJS     = $(addprefix $(BUILD_DIR)/, $(VI_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
PTHREAD_TEST_OBJS = $(BUILD_DIR)/bin/pthread_test.o $(CRT0_OBJ) $(LIBC_OBJS)

INIT_ELF = $(BUILD_DIR)/init.elf
SH_ELF = $(BUILD_DIR)/sh.elf
ECHO_ELF = $(BUILD_DIR)/echo.elf
LS_ELF = $(BUILD_DIR)/ls.elf
CAT_ELF = $(BUILD_DIR)/cat.elf
TRUE_ELF = $(BUILD_DIR)/true.elf
IFCONFIG_ELF = $(BUILD_DIR)/ifconfig.elf
PING_ELF = $(BUILD_DIR)/ping.elf
HWLIST_ELF = $(BUILD_DIR)/hwlist.elf
DEVREG_ELF = $(BUILD_DIR)/devreg.elf
FABRICLS_ELF = $(BUILD_DIR)/fabricls.elf
FABRICEVENTS_ELF = $(BUILD_DIR)/fabricevents.elf
FABRICNETCHECK_ELF = $(BUILD_DIR)/fabricnetcheck.elf
HOSTINFO_ELF = $(BUILD_DIR)/hostinfo.elf
CPUINFO_ELF = $(BUILD_DIR)/cpuinfo.elf
DISKINFO_ELF = $(BUILD_DIR)/diskinfo.elf
KMODCTL_ELF = $(BUILD_DIR)/kmodctl.elf
SLEEP_ELF = $(BUILD_DIR)/sleep.elf
SIGTEST_ELF = $(BUILD_DIR)/sigtest.elf
STTY_ELF = $(BUILD_DIR)/stty.elf
TIMECHECK_ELF = $(BUILD_DIR)/timecheck.elf
DATE_ELF = $(BUILD_DIR)/date.elf
HWCLOCK_ELF = $(BUILD_DIR)/hwclock.elf
SYSCALLTEST_ELF = $(BUILD_DIR)/syscalltest.elf
TTYREADTEST_ELF = $(BUILD_DIR)/ttyreadtest.elf
SCSTAT_ELF = $(BUILD_DIR)/scstat.elf
STDIO_SMOKE_ELF = $(BUILD_DIR)/stdio_smoke.elf
POLLTEST_ELF = $(BUILD_DIR)/polltest.elf
SELECTTEST_ELF = $(BUILD_DIR)/selecttest.elf
FUTEXTEST_ELF = $(BUILD_DIR)/futextest.elf
PIPETEST_ELF = $(BUILD_DIR)/pipetest.elf
UDPTEST_ELF = $(BUILD_DIR)/udptest.elf
TCPTEST_ELF = $(BUILD_DIR)/tcptest.elf
FSAPITEST_ELF = $(BUILD_DIR)/fsapitest.elf
FORKTEST_ELF = $(BUILD_DIR)/forktest.elf
EXECVETEST_ELF = $(BUILD_DIR)/execvetest.elf
CONTRACT_FD_ELF = $(BUILD_DIR)/contract_fd.elf
CONTRACT_FD_INHERIT_ELF = $(BUILD_DIR)/contract_fd_inherit.elf
CONTRACT_SPAWN_WAIT_ELF = $(BUILD_DIR)/contract_spawn_wait.elf
CONTRACT_EXEC_PROBE_ELF = $(BUILD_DIR)/contract_exec_probe.elf
CONTRACT_EXEC_AFTER_ELF = $(BUILD_DIR)/contract_exec_after.elf
CONTRACT_HEAP_ELF = $(BUILD_DIR)/contract_heap.elf
CONTRACT_DIRENT_ELF = $(BUILD_DIR)/contract_dirent.elf
CONTRACT_FSIO_ELF = $(BUILD_DIR)/contract_fsio.elf
CONTRACT_WAIT_NONCHILD_ELF = $(BUILD_DIR)/contract_wait_nonchild.elf
CP_ELF   = $(BUILD_DIR)/cp.elf
MV_ELF   = $(BUILD_DIR)/mv.elf
RM_ELF   = $(BUILD_DIR)/rm.elf
GREP_ELF = $(BUILD_DIR)/grep.elf
FIND_ELF   = $(BUILD_DIR)/find.elf
LOGIN_ELF  = $(BUILD_DIR)/login.elf
PASSWD_ELF = $(BUILD_DIR)/passwd.elf
ID_ELF     = $(BUILD_DIR)/id.elf
GROUPS_ELF = $(BUILD_DIR)/groups.elf
WHOAMI_ELF = $(BUILD_DIR)/whoami.elf
PS_ELF     = $(BUILD_DIR)/ps.elf
TOP_ELF    = $(BUILD_DIR)/top.elf
VI_ELF = $(BUILD_DIR)/vi.elf
PTHREAD_TEST_ELF = $(BUILD_DIR)/pthread_test.elf
INIT_BIN   = $(BIN_DIR)/init
LOGIN_BIN  = $(BIN_DIR)/login
PASSWD_BIN = $(BIN_DIR)/passwd
ID_BIN     = $(BIN_DIR)/id
GROUPS_BIN = $(BIN_DIR)/groups
WHOAMI_BIN = $(BIN_DIR)/whoami
PS_BIN     = $(BIN_DIR)/ps
TOP_BIN    = $(BIN_DIR)/top
VI_BIN = $(BIN_DIR)/vi
SH_BIN = $(BIN_DIR)/sh
ECHO_BIN = $(BIN_DIR)/echo
LS_BIN = $(BIN_DIR)/ls
CAT_BIN = $(BIN_DIR)/cat
TRUE_BIN = $(BIN_DIR)/true
IFCONFIG_BIN = $(BIN_DIR)/ifconfig
PING_BIN = $(BIN_DIR)/ping
HWLIST_BIN = $(BIN_DIR)/hwlist
DEVREG_BIN = $(BIN_DIR)/devreg
FABRICLS_BIN = $(BIN_DIR)/fabricls
FABRICEVENTS_BIN = $(BIN_DIR)/fabricevents
FABRICNETCHECK_BIN = $(BIN_DIR)/fabricnetcheck
HOSTINFO_BIN = $(BIN_DIR)/hostinfo
CPUINFO_BIN = $(BIN_DIR)/cpuinfo
DISKINFO_BIN = $(BIN_DIR)/diskinfo
KMODCTL_BIN = $(BIN_DIR)/kmodctl
SLEEP_BIN = $(BIN_DIR)/sleep
MODULES_DIR = $(ROOTFS_DIR)/lib/modules
DEMO_KMOD = $(MODULES_DIR)/demo.kmod
DEMO_KO_OBJ = $(BUILD_DIR)/modules/demo_echo.o
DEMO_KO = $(MODULES_DIR)/demo.ko
SIGTEST_BIN = $(BIN_DIR)/sigtest
STTY_BIN = $(BIN_DIR)/stty
TIMECHECK_BIN = $(BIN_DIR)/timecheck
DATE_BIN = $(BIN_DIR)/date
HWCLOCK_BIN = $(BIN_DIR)/hwclock
SYSCALLTEST_BIN = $(BIN_DIR)/syscalltest
TTYREADTEST_BIN = $(BIN_DIR)/ttyreadtest
SCSTAT_BIN = $(BIN_DIR)/scstat
STDIO_SMOKE_BIN = $(BIN_DIR)/stdio_smoke
POLLTEST_BIN = $(BIN_DIR)/polltest
SELECTTEST_BIN = $(BIN_DIR)/selecttest
FUTEXTEST_BIN = $(BIN_DIR)/futextest
PIPETEST_BIN = $(BIN_DIR)/pipetest
UDPTEST_BIN = $(BIN_DIR)/udptest
TCPTEST_BIN = $(BIN_DIR)/tcptest
FSAPITEST_BIN = $(BIN_DIR)/fsapitest
FORKTEST_BIN = $(BIN_DIR)/forktest
EXECVETEST_BIN = $(BIN_DIR)/execvetest
CONTRACT_FD_BIN = $(BIN_DIR)/contract_fd
CONTRACT_FD_INHERIT_BIN = $(BIN_DIR)/contract_fd_inherit
CONTRACT_SPAWN_WAIT_BIN = $(BIN_DIR)/contract_spawn_wait
CONTRACT_EXEC_PROBE_BIN = $(BIN_DIR)/contract_exec_probe
CONTRACT_EXEC_AFTER_BIN = $(BIN_DIR)/contract_exec_after
CONTRACT_HEAP_BIN = $(BIN_DIR)/contract_heap
CONTRACT_DIRENT_BIN = $(BIN_DIR)/contract_dirent
CONTRACT_FSIO_BIN = $(BIN_DIR)/contract_fsio
CONTRACT_WAIT_NONCHILD_BIN = $(BIN_DIR)/contract_wait_nonchild
CP_BIN   = $(BIN_DIR)/cp
MV_BIN   = $(BIN_DIR)/mv
RM_BIN   = $(BIN_DIR)/rm
GREP_BIN = $(BIN_DIR)/grep
FIND_BIN = $(BIN_DIR)/find
PTHREAD_TEST_BIN = $(BIN_DIR)/pthread_test

.PHONY: all clean check-bsd-abi sync-bsd-abi

all: check-bsd-abi $(INIT_BIN) $(LOGIN_BIN) $(SH_BIN) $(ECHO_BIN) $(LS_BIN) $(CAT_BIN) $(TRUE_BIN) $(IFCONFIG_BIN) $(PING_BIN) $(HWLIST_BIN) $(DEVREG_BIN) $(FABRICLS_BIN) $(FABRICEVENTS_BIN) $(FABRICNETCHECK_BIN) $(HOSTINFO_BIN) $(CPUINFO_BIN) $(DISKINFO_BIN) $(KMODCTL_BIN) $(SLEEP_BIN) $(SIGTEST_BIN) $(STTY_BIN) $(TIMECHECK_BIN) $(DATE_BIN) $(HWCLOCK_BIN) $(SYSCALLTEST_BIN) $(TTYREADTEST_BIN) $(SCSTAT_BIN) $(STDIO_SMOKE_BIN) $(POLLTEST_BIN) $(SELECTTEST_BIN) $(FUTEXTEST_BIN) $(PIPETEST_BIN) $(UDPTEST_BIN) $(TCPTEST_BIN) $(FSAPITEST_BIN) $(FORKTEST_BIN) $(EXECVETEST_BIN) $(CONTRACT_FD_BIN) $(CONTRACT_FD_INHERIT_BIN) $(CONTRACT_SPAWN_WAIT_BIN) $(CONTRACT_EXEC_PROBE_BIN) $(CONTRACT_EXEC_AFTER_BIN) $(CONTRACT_HEAP_BIN) $(CONTRACT_DIRENT_BIN) $(CONTRACT_FSIO_BIN) $(CONTRACT_WAIT_NONCHILD_BIN) $(DEMO_KMOD) $(DEMO_KO) $(CP_BIN) $(MV_BIN) $(RM_BIN) $(GREP_BIN) $(FIND_BIN) $(PASSWD_BIN) $(ID_BIN) $(GROUPS_BIN) $(WHOAMI_BIN) $(VI_BIN) $(PTHREAD_TEST_BIN) $(PS_BIN) $(TOP_BIN)

check-bsd-abi:
	@python3 ../scripts/check_bsd_abi_headers.py

sync-bsd-abi:
	@python3 ../scripts/sync_bsd_abi_headers.py

$(BUILD_DIR)/crt0.o: crt0.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Auto-generated header dependencies from -MMD -MP.
DEPFILES := $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
-include $(DEPFILES)

$(INIT_ELF): $(INIT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(INIT_OBJS)

$(LOGIN_ELF): $(LOGIN_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(LOGIN_OBJS)

$(SH_ELF): $(SH_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SH_OBJS)

$(ECHO_ELF): $(ECHO_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(ECHO_OBJS)

$(LS_ELF): $(LS_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(LS_OBJS)

$(CAT_ELF): $(CAT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CAT_OBJS)

$(TRUE_ELF): $(TRUE_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(TRUE_OBJS)

$(IFCONFIG_ELF): $(IFCONFIG_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(IFCONFIG_OBJS)

$(PING_ELF): $(PING_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(PING_OBJS)

$(HWLIST_ELF): $(HWLIST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(HWLIST_OBJS)

$(DEVREG_ELF): $(DEVREG_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(DEVREG_OBJS)

$(FABRICLS_ELF): $(FABRICLS_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(FABRICLS_OBJS)

$(FABRICEVENTS_ELF): $(FABRICEVENTS_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(FABRICEVENTS_OBJS)

$(FABRICNETCHECK_ELF): $(FABRICNETCHECK_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(FABRICNETCHECK_OBJS)

$(HOSTINFO_ELF): $(HOSTINFO_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(HOSTINFO_OBJS)

$(CPUINFO_ELF): $(CPUINFO_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CPUINFO_OBJS)

$(DISKINFO_ELF): $(DISKINFO_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(DISKINFO_OBJS)

$(KMODCTL_ELF): $(KMODCTL_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(KMODCTL_OBJS)

$(SLEEP_ELF): $(SLEEP_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SLEEP_OBJS)

$(SIGTEST_ELF): $(SIGTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SIGTEST_OBJS)

$(STTY_ELF): $(STTY_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(STTY_OBJS)

$(TIMECHECK_ELF): $(TIMECHECK_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(TIMECHECK_OBJS)

$(DATE_ELF): $(DATE_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(DATE_OBJS)

$(HWCLOCK_ELF): $(HWCLOCK_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(HWCLOCK_OBJS)

$(SYSCALLTEST_ELF): $(SYSCALLTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SYSCALLTEST_OBJS)

$(TTYREADTEST_ELF): $(TTYREADTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(TTYREADTEST_OBJS)

$(SCSTAT_ELF): $(SCSTAT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SCSTAT_OBJS)

$(STDIO_SMOKE_ELF): $(STDIO_SMOKE_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(STDIO_SMOKE_OBJS)

$(POLLTEST_ELF): $(POLLTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(POLLTEST_OBJS)

$(SELECTTEST_ELF): $(SELECTTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SELECTTEST_OBJS)

$(FUTEXTEST_ELF): $(FUTEXTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(FUTEXTEST_OBJS)

$(PIPETEST_ELF): $(PIPETEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(PIPETEST_OBJS)

$(UDPTEST_ELF): $(UDPTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(UDPTEST_OBJS)

$(TCPTEST_ELF): $(TCPTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(TCPTEST_OBJS)

$(FSAPITEST_ELF): $(FSAPITEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(FSAPITEST_OBJS)

$(FORKTEST_ELF): $(FORKTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(FORKTEST_OBJS)

$(EXECVETEST_ELF): $(EXECVETEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(EXECVETEST_OBJS)

$(CONTRACT_FD_ELF): $(CONTRACT_FD_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_FD_OBJS)

$(CONTRACT_FD_INHERIT_ELF): $(CONTRACT_FD_INHERIT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_FD_INHERIT_OBJS)

$(CONTRACT_SPAWN_WAIT_ELF): $(CONTRACT_SPAWN_WAIT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_SPAWN_WAIT_OBJS)

$(CONTRACT_EXEC_PROBE_ELF): $(CONTRACT_EXEC_PROBE_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_EXEC_PROBE_OBJS)

$(CONTRACT_EXEC_AFTER_ELF): $(CONTRACT_EXEC_AFTER_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_EXEC_AFTER_OBJS)

$(CONTRACT_HEAP_ELF): $(CONTRACT_HEAP_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_HEAP_OBJS)

$(CONTRACT_DIRENT_ELF): $(CONTRACT_DIRENT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_DIRENT_OBJS)

$(CONTRACT_FSIO_ELF): $(CONTRACT_FSIO_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_FSIO_OBJS)

$(CONTRACT_WAIT_NONCHILD_ELF): $(CONTRACT_WAIT_NONCHILD_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CONTRACT_WAIT_NONCHILD_OBJS)

$(CP_ELF): $(CP_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CP_OBJS)

$(MV_ELF): $(MV_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(MV_OBJS)

$(RM_ELF): $(RM_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(RM_OBJS)

$(GREP_ELF): $(GREP_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(GREP_OBJS)

$(FIND_ELF): $(FIND_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(FIND_OBJS)

$(PASSWD_ELF): $(PASSWD_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(PASSWD_OBJS)

$(ID_ELF): $(ID_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(ID_OBJS)

$(GROUPS_ELF): $(GROUPS_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(GROUPS_OBJS)

$(WHOAMI_ELF): $(WHOAMI_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(WHOAMI_OBJS)

$(PS_ELF): $(PS_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(PS_OBJS)

$(TOP_ELF): $(TOP_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(TOP_OBJS)

$(INIT_BIN): $(INIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(LOGIN_BIN): $(LOGIN_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(SH_BIN): $(SH_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(ECHO_BIN): $(ECHO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(LS_BIN): $(LS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CAT_BIN): $(CAT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(TRUE_BIN): $(TRUE_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(IFCONFIG_BIN): $(IFCONFIG_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(PING_BIN): $(PING_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(HWLIST_BIN): $(HWLIST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(DEVREG_BIN): $(DEVREG_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(FABRICLS_BIN): $(FABRICLS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(FABRICEVENTS_BIN): $(FABRICEVENTS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(FABRICNETCHECK_BIN): $(FABRICNETCHECK_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(HOSTINFO_BIN): $(HOSTINFO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CPUINFO_BIN): $(CPUINFO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(DISKINFO_BIN): $(DISKINFO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(KMODCTL_BIN): $(KMODCTL_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(DEMO_KMOD): ../scripts/mkkmod.py
	@mkdir -p $(MODULES_DIR)
	python3 ../scripts/mkkmod.py --output $@ --name demo.echo --kind misc --version 0.1 --flags 0

$(DEMO_KO_OBJ): modules/demo_echo.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I../include -fno-asynchronous-unwind-tables -fno-unwind-tables -c $< -o $@

$(DEMO_KO): $(DEMO_KO_OBJ)
	@mkdir -p $(MODULES_DIR)
	cp $(DEMO_KO_OBJ) $@

$(SLEEP_BIN): $(SLEEP_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(SIGTEST_BIN): $(SIGTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(STTY_BIN): $(STTY_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(TIMECHECK_BIN): $(TIMECHECK_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(DATE_BIN): $(DATE_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(HWCLOCK_BIN): $(HWCLOCK_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(SYSCALLTEST_BIN): $(SYSCALLTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(TTYREADTEST_BIN): $(TTYREADTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(SCSTAT_BIN): $(SCSTAT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(STDIO_SMOKE_BIN): $(STDIO_SMOKE_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(POLLTEST_BIN): $(POLLTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(SELECTTEST_BIN): $(SELECTTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(FUTEXTEST_BIN): $(FUTEXTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(PIPETEST_BIN): $(PIPETEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(UDPTEST_BIN): $(UDPTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(TCPTEST_BIN): $(TCPTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(FSAPITEST_BIN): $(FSAPITEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(FORKTEST_BIN): $(FORKTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(EXECVETEST_BIN): $(EXECVETEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_FD_BIN): $(CONTRACT_FD_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_FD_INHERIT_BIN): $(CONTRACT_FD_INHERIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_SPAWN_WAIT_BIN): $(CONTRACT_SPAWN_WAIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_EXEC_PROBE_BIN): $(CONTRACT_EXEC_PROBE_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_EXEC_AFTER_BIN): $(CONTRACT_EXEC_AFTER_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_HEAP_BIN): $(CONTRACT_HEAP_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_DIRENT_BIN): $(CONTRACT_DIRENT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_FSIO_BIN): $(CONTRACT_FSIO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CONTRACT_WAIT_NONCHILD_BIN): $(CONTRACT_WAIT_NONCHILD_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(CP_BIN): $(CP_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(MV_BIN): $(MV_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(RM_BIN): $(RM_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(GREP_BIN): $(GREP_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(FIND_BIN): $(FIND_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(PASSWD_BIN): $(PASSWD_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(ID_BIN): $(ID_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(GROUPS_BIN): $(GROUPS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(WHOAMI_BIN): $(WHOAMI_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(PS_BIN): $(PS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(TOP_BIN): $(TOP_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(VI_ELF): $(VI_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(VI_OBJS)

$(VI_BIN): $(VI_ELF)
	@mkdir -p $(BIN_DIR)
	rm -f $@
	cp $< $@
	chmod 0755 $@

$(PTHREAD_TEST_ELF): $(PTHREAD_TEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(PTHREAD_TEST_OBJS)

$(PTHREAD_TEST_BIN): $(PTHREAD_TEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

# ── Rust utilities ────────────────────────────────────────────────────────────
#
# Requirements:
#   rustup target add x86_64-unknown-none
#
# Build separately (not part of default `all`, rust toolchain may be absent):
#   make rust-utils
#
# RUSTC can be overridden: make rust-utils RUSTC=~/.cargo/bin/rustc
#
RUSTC ?= rustc

RUST_HELLO_RS  = rust/hello/main.rs
RUST_HELLO_LIB = $(BUILD_DIR)/rust/hello/libhello.a
RUST_HELLO_ELF = $(BUILD_DIR)/rust_hello.elf
RUST_HELLO_BIN = $(BIN_DIR)/rust_hello
RUST_ECHO_RS   = rust/echo/main.rs
RUST_ECHO_LIB  = $(BUILD_DIR)/rust/echo/librust_echo.a
RUST_ECHO_ELF  = $(BUILD_DIR)/rust_echo.elf
RUST_ECHO_BIN  = $(BIN_DIR)/rust_echo

$(RUST_HELLO_LIB): $(RUST_HELLO_RS)
	@mkdir -p $(dir $@)
	$(RUSTC) --edition 2021 \
	    --target x86_64-unknown-none \
	    --crate-type staticlib \
	    --crate-name hello \
	    -C opt-level=2 \
	    -C panic=abort \
	    -o $@ \
	    $<

$(RUST_ECHO_LIB): $(RUST_ECHO_RS)
	@mkdir -p $(dir $@)
	$(RUSTC) --edition 2021 \
	    --target x86_64-unknown-none \
	    --crate-type staticlib \
	    --crate-name rust_echo \
	    -C opt-level=2 \
	    -C panic=abort \
	    -o $@ \
	    $<

$(RUST_HELLO_ELF): $(RUST_HELLO_LIB) $(CRT0_OBJ) $(LIBC_OBJS) rust/link_rust.ld link.ld
	@mkdir -p $(dir $@)
	$(LD) -T rust/link_rust.ld -o $@ $(CRT0_OBJ) $(LIBC_OBJS) $(RUST_HELLO_LIB)

$(RUST_ECHO_ELF): $(RUST_ECHO_LIB) $(CRT0_OBJ) $(LIBC_OBJS) rust/link_rust.ld link.ld
	@mkdir -p $(dir $@)
	$(LD) -T rust/link_rust.ld -o $@ $(CRT0_OBJ) $(LIBC_OBJS) $(RUST_ECHO_LIB)

$(RUST_HELLO_BIN): $(RUST_HELLO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

$(RUST_ECHO_BIN): $(RUST_ECHO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@
	chmod 0755 $@

.PHONY: rust-utils
rust-utils: $(RUST_HELLO_BIN) $(RUST_ECHO_BIN)

# ── clean ─────────────────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)
