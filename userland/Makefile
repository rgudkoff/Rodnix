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

USERLAND_C_SRCS += \
	bootstrap/bootstrap.c

SRCS = $(USERLAND_C_SRCS)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o)) $(BUILD_DIR)/crt0.o

INIT_ELF = $(BUILD_DIR)/init.elf
INIT_BIN = $(BIN_DIR)/init

.PHONY: all clean

all: $(INIT_BIN)

$(BUILD_DIR)/crt0.o: crt0.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(INIT_ELF): $(OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(INIT_BIN): $(INIT_ELF)
	@mkdir -p $(BIN_DIR)
	cp $< $@

clean:
	rm -rf $(BUILD_DIR)
