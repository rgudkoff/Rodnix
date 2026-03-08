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
CONTRACT_FD_SRCS = bin/contract_fd.c
CONTRACT_FD_INHERIT_SRCS = bin/contract_fd_inherit.c
CONTRACT_SPAWN_WAIT_SRCS = bin/contract_spawn_wait.c
CONTRACT_EXEC_PROBE_SRCS = bin/contract_exec_probe.c
CONTRACT_EXEC_AFTER_SRCS = bin/contract_exec_after.c

CRT0_OBJ = $(BUILD_DIR)/crt0.o
INIT_OBJS = $(addprefix $(BUILD_DIR)/, $(INIT_SRCS:.c=.o)) $(CRT0_OBJ)
SH_OBJS = $(addprefix $(BUILD_DIR)/, $(SH_SRCS:.c=.o)) $(CRT0_OBJ)
ECHO_OBJS = $(addprefix $(BUILD_DIR)/, $(ECHO_SRCS:.c=.o)) $(CRT0_OBJ)
LS_OBJS = $(addprefix $(BUILD_DIR)/, $(LS_SRCS:.c=.o)) $(CRT0_OBJ)
CAT_OBJS = $(addprefix $(BUILD_DIR)/, $(CAT_SRCS:.c=.o)) $(CRT0_OBJ)
TRUE_OBJS = $(addprefix $(BUILD_DIR)/, $(TRUE_SRCS:.c=.o)) $(CRT0_OBJ)
IFCONFIG_OBJS = $(addprefix $(BUILD_DIR)/, $(IFCONFIG_SRCS:.c=.o)) $(CRT0_OBJ)
HWLIST_OBJS = $(addprefix $(BUILD_DIR)/, $(HWLIST_SRCS:.c=.o)) $(CRT0_OBJ)
FABRICLS_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICLS_SRCS:.c=.o)) $(CRT0_OBJ)
FABRICEVENTS_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICEVENTS_SRCS:.c=.o)) $(CRT0_OBJ)
FABRICNETCHECK_OBJS = $(addprefix $(BUILD_DIR)/, $(FABRICNETCHECK_SRCS:.c=.o)) $(CRT0_OBJ)
CONTRACT_FD_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_FD_SRCS:.c=.o)) $(CRT0_OBJ)
CONTRACT_FD_INHERIT_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_FD_INHERIT_SRCS:.c=.o)) $(CRT0_OBJ)
CONTRACT_SPAWN_WAIT_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_SPAWN_WAIT_SRCS:.c=.o)) $(CRT0_OBJ)
CONTRACT_EXEC_PROBE_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_EXEC_PROBE_SRCS:.c=.o)) $(CRT0_OBJ)
CONTRACT_EXEC_AFTER_OBJS = $(addprefix $(BUILD_DIR)/, $(CONTRACT_EXEC_AFTER_SRCS:.c=.o)) $(CRT0_OBJ)

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
CONTRACT_FD_ELF = $(BUILD_DIR)/contract_fd.elf
CONTRACT_FD_INHERIT_ELF = $(BUILD_DIR)/contract_fd_inherit.elf
CONTRACT_SPAWN_WAIT_ELF = $(BUILD_DIR)/contract_spawn_wait.elf
CONTRACT_EXEC_PROBE_ELF = $(BUILD_DIR)/contract_exec_probe.elf
CONTRACT_EXEC_AFTER_ELF = $(BUILD_DIR)/contract_exec_after.elf
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
CONTRACT_FD_BIN = $(BIN_DIR)/contract_fd
CONTRACT_FD_INHERIT_BIN = $(BIN_DIR)/contract_fd_inherit
CONTRACT_SPAWN_WAIT_BIN = $(BIN_DIR)/contract_spawn_wait
CONTRACT_EXEC_PROBE_BIN = $(BIN_DIR)/contract_exec_probe
CONTRACT_EXEC_AFTER_BIN = $(BIN_DIR)/contract_exec_after

.PHONY: all clean check-bsd-abi sync-bsd-abi

all: check-bsd-abi $(INIT_BIN) $(SH_BIN) $(ECHO_BIN) $(LS_BIN) $(CAT_BIN) $(TRUE_BIN) $(IFCONFIG_BIN) $(HWLIST_BIN) $(FABRICLS_BIN) $(FABRICEVENTS_BIN) $(FABRICNETCHECK_BIN) $(CONTRACT_FD_BIN) $(CONTRACT_FD_INHERIT_BIN) $(CONTRACT_SPAWN_WAIT_BIN) $(CONTRACT_EXEC_PROBE_BIN) $(CONTRACT_EXEC_AFTER_BIN)

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

clean:
	rm -rf $(BUILD_DIR)
