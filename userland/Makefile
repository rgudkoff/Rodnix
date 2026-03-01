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

CRT0_OBJ = $(BUILD_DIR)/crt0.o
INIT_OBJS = $(addprefix $(BUILD_DIR)/, $(INIT_SRCS:.c=.o)) $(CRT0_OBJ)
SH_OBJS = $(addprefix $(BUILD_DIR)/, $(SH_SRCS:.c=.o)) $(CRT0_OBJ)

INIT_ELF = $(BUILD_DIR)/init.elf
SH_ELF = $(BUILD_DIR)/sh.elf
INIT_BIN = $(BIN_DIR)/init
SH_BIN = $(BIN_DIR)/sh

.PHONY: all clean

all: $(INIT_BIN) $(SH_BIN)

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

$(INIT_BIN): $(INIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

$(SH_BIN): $(SH_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

clean:
	rm -rf $(BUILD_DIR)
