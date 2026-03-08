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

CRT0_OBJ = $(BUILD_DIR)/crt0.o
INIT_OBJS = $(addprefix $(BUILD_DIR)/, $(INIT_SRCS:.c=.o)) $(CRT0_OBJ)
SH_OBJS = $(addprefix $(BUILD_DIR)/, $(SH_SRCS:.c=.o)) $(CRT0_OBJ)
ECHO_OBJS = $(addprefix $(BUILD_DIR)/, $(ECHO_SRCS:.c=.o)) $(CRT0_OBJ)
LS_OBJS = $(addprefix $(BUILD_DIR)/, $(LS_SRCS:.c=.o)) $(CRT0_OBJ)
CAT_OBJS = $(addprefix $(BUILD_DIR)/, $(CAT_SRCS:.c=.o)) $(CRT0_OBJ)
TRUE_OBJS = $(addprefix $(BUILD_DIR)/, $(TRUE_SRCS:.c=.o)) $(CRT0_OBJ)

INIT_ELF = $(BUILD_DIR)/init.elf
SH_ELF = $(BUILD_DIR)/sh.elf
ECHO_ELF = $(BUILD_DIR)/echo.elf
LS_ELF = $(BUILD_DIR)/ls.elf
CAT_ELF = $(BUILD_DIR)/cat.elf
TRUE_ELF = $(BUILD_DIR)/true.elf
INIT_BIN = $(BIN_DIR)/init
SH_BIN = $(BIN_DIR)/sh
ECHO_BIN = $(BIN_DIR)/echo
LS_BIN = $(BIN_DIR)/ls
CAT_BIN = $(BIN_DIR)/cat
TRUE_BIN = $(BIN_DIR)/true

.PHONY: all clean check-bsd-abi

all: check-bsd-abi $(INIT_BIN) $(SH_BIN) $(ECHO_BIN) $(LS_BIN) $(CAT_BIN) $(TRUE_BIN)

check-bsd-abi:
	@python3 ../scripts/check_bsd_abi_headers.py

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

clean:
	rm -rf $(BUILD_DIR)
