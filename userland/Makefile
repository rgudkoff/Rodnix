SHELL := /bin/bash

CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld

CFLAGS = -m64 \
         -ffreestanding \
         -fno-builtin \
         -fno-stack-protector \
         -nostdlib \
         -O2 \
         -g \
         -Wall \
         -Wextra \
         -I./include

ASFLAGS = -f elf64
LDFLAGS = -T link.ld

BUILD_DIR = build
ROOTFS_DIR = rootfs
BIN_DIR = $(ROOTFS_DIR)/bin

INIT_SRCS = init/init.c
SH_SRCS = shell/shell.c
ECHO_SRCS = bin/echo.c
LS_SRCS = bin/ls.c
CAT_SRCS = bin/cat.c
TRUE_SRCS = bin/true.c
IFCONFIG_SRCS = bin/ifconfig.c
HWLIST_SRCS = bin/hwlist.c
FABRICLS_SRCS = bin/fabricls.c
FABRICEVENTS_SRCS = bin/fabricevents.c
FABRICNETCHECK_SRCS = bin/fabricnetcheck.c
HOSTINFO_SRCS = bin/hostinfo.c
TIMECHECK_SRCS = bin/timecheck.c
SYSCALLTEST_SRCS = bin/syscalltest.c
TTYREADTEST_SRCS = bin/ttyreadtest.c
SCSTAT_SRCS = bin/scstat.c
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

CRT0_OBJ = $(BUILD_DIR)/crt0.o
LIBC_SRCS = libc/errno.c libc/string.c libc/ctype.c libc/stdlib.c libc/stdio.c libc/malloc.c libc/dirent.c
LIBC_OBJS = $(addprefix $(BUILD_DIR)/, $(LIBC_SRCS:.c=.o))

INIT_OBJS = $(addprefix $(BUILD_DIR)/, $(INIT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SH_OBJS = $(addprefix $(BUILD_DIR)/, $(SH_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
ECHO_OBJS = $(addprefix $(BUILD_DIR)/, $(ECHO_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
LS_OBJS = $(addprefix $(BUILD_DIR)/, $(LS_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
CAT_OBJS = $(addprefix $(BUILD_DIR)/, $(CAT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
TRUE_OBJS = $(addprefix $(BUILD_DIR)/, $(TRUE_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
IFCONFIG_OBJS = $(addprefix $(BUILD_DIR)/, $(IFCONFIG_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
HWLIST_OBJS = $(addprefix $(BUILD_DIR)/, $(HWLIST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FABRICLS_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICLS_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FABRICEVENTS_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICEVENTS_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
FABRICNETCHECK_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICNETCHECK_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
HOSTINFO_OBJS = $(addprefix $(BUILD_DIR)/, $(HOSTINFO_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
TIMECHECK_OBJS = $(addprefix $(BUILD_DIR)/, $(TIMECHECK_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SYSCALLTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(SYSCALLTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
TTYREADTEST_OBJS = $(addprefix $(BUILD_DIR)/, $(TTYREADTEST_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
SCSTAT_OBJS = $(addprefix $(BUILD_DIR)/, $(SCSTAT_SRCS:.c=.o)) $(CRT0_OBJ) $(LIBC_OBJS)
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

INIT_ELF = $(BUILD_DIR)/init.elf
SH_ELF = $(BUILD_DIR)/sh.elf
ECHO_ELF = $(BUILD_DIR)/echo.elf
LS_ELF = $(BUILD_DIR)/ls.elf
CAT_ELF = $(BUILD_DIR)/cat.elf
TRUE_ELF = $(BUILD_DIR)/true.elf
IFCONFIG_ELF = $(BUILD_DIR)/ifconfig.elf
HWLIST_ELF = $(BUILD_DIR)/hwlist.elf
FABRICLS_ELF = $(BUILD_DIR)/fabricls.elf
FABRICEVENTS_ELF = $(BUILD_DIR)/fabricevents.elf
FABRICNETCHECK_ELF = $(BUILD_DIR)/fabricnetcheck.elf
HOSTINFO_ELF = $(BUILD_DIR)/hostinfo.elf
TIMECHECK_ELF = $(BUILD_DIR)/timecheck.elf
SYSCALLTEST_ELF = $(BUILD_DIR)/syscalltest.elf
TTYREADTEST_ELF = $(BUILD_DIR)/ttyreadtest.elf
SCSTAT_ELF = $(BUILD_DIR)/scstat.elf
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
INIT_BIN = $(BIN_DIR)/init
SH_BIN = $(BIN_DIR)/sh
ECHO_BIN = $(BIN_DIR)/echo
LS_BIN = $(BIN_DIR)/ls
CAT_BIN = $(BIN_DIR)/cat
TRUE_BIN = $(BIN_DIR)/true
IFCONFIG_BIN = $(BIN_DIR)/ifconfig
HWLIST_BIN = $(BIN_DIR)/hwlist
FABRICLS_BIN = $(BIN_DIR)/fabricls
FABRICEVENTS_BIN = $(BIN_DIR)/fabricevents
FABRICNETCHECK_BIN = $(BIN_DIR)/fabricnetcheck
HOSTINFO_BIN = $(BIN_DIR)/hostinfo
TIMECHECK_BIN = $(BIN_DIR)/timecheck
SYSCALLTEST_BIN = $(BIN_DIR)/syscalltest
TTYREADTEST_BIN = $(BIN_DIR)/ttyreadtest
SCSTAT_BIN = $(BIN_DIR)/scstat
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

.PHONY: all clean check-bsd-abi sync-bsd-abi

all: check-bsd-abi $(INIT_BIN) $(SH_BIN) $(ECHO_BIN) $(LS_BIN) $(CAT_BIN) $(TRUE_BIN) $(IFCONFIG_BIN) $(HWLIST_BIN) $(FABRICLS_BIN) $(FABRICEVENTS_BIN) $(FABRICNETCHECK_BIN) $(HOSTINFO_BIN) $(TIMECHECK_BIN) $(SYSCALLTEST_BIN) $(TTYREADTEST_BIN) $(SCSTAT_BIN) $(FORKTEST_BIN) $(EXECVETEST_BIN) $(CONTRACT_FD_BIN) $(CONTRACT_FD_INHERIT_BIN) $(CONTRACT_SPAWN_WAIT_BIN) $(CONTRACT_EXEC_PROBE_BIN) $(CONTRACT_EXEC_AFTER_BIN) $(CONTRACT_HEAP_BIN) $(CONTRACT_DIRENT_BIN) $(CONTRACT_FSIO_BIN) $(CONTRACT_WAIT_NONCHILD_BIN)

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


# Auto-generated header dependencies from -MMD -MP.
DEPFILES := $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
-include $(DEPFILES)

$(INIT_ELF): $(INIT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(INIT_OBJS)

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

$(HWLIST_ELF): $(HWLIST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(HWLIST_OBJS)

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

$(TIMECHECK_ELF): $(TIMECHECK_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(TIMECHECK_OBJS)

$(SYSCALLTEST_ELF): $(SYSCALLTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SYSCALLTEST_OBJS)

$(TTYREADTEST_ELF): $(TTYREADTEST_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(TTYREADTEST_OBJS)

$(SCSTAT_ELF): $(SCSTAT_OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(SCSTAT_OBJS)

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

$(INIT_BIN): $(INIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(SH_BIN): $(SH_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(ECHO_BIN): $(ECHO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(LS_BIN): $(LS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CAT_BIN): $(CAT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(TRUE_BIN): $(TRUE_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(IFCONFIG_BIN): $(IFCONFIG_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(HWLIST_BIN): $(HWLIST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(FABRICLS_BIN): $(FABRICLS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(FABRICEVENTS_BIN): $(FABRICEVENTS_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(FABRICNETCHECK_BIN): $(FABRICNETCHECK_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(HOSTINFO_BIN): $(HOSTINFO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(TIMECHECK_BIN): $(TIMECHECK_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(SYSCALLTEST_BIN): $(SYSCALLTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(TTYREADTEST_BIN): $(TTYREADTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(SCSTAT_BIN): $(SCSTAT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(FORKTEST_BIN): $(FORKTEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(EXECVETEST_BIN): $(EXECVETEST_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_FD_BIN): $(CONTRACT_FD_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_FD_INHERIT_BIN): $(CONTRACT_FD_INHERIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_SPAWN_WAIT_BIN): $(CONTRACT_SPAWN_WAIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_EXEC_PROBE_BIN): $(CONTRACT_EXEC_PROBE_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_EXEC_AFTER_BIN): $(CONTRACT_EXEC_AFTER_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_HEAP_BIN): $(CONTRACT_HEAP_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_DIRENT_BIN): $(CONTRACT_DIRENT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_FSIO_BIN): $(CONTRACT_FSIO_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(CONTRACT_WAIT_NONCHILD_BIN): $(CONTRACT_WAIT_NONCHILD_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

clean:
	rm -rf $(BUILD_DIR)
