# RodNIX Makefile
# Builds 64-bit kernel for x86_64 architecture

# Toolchain
CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy

# Directories
BUILD_DIR = build
ISO_DIR = iso
KERNEL_DIR = kernel
BOOT_DIR = boot
INCLUDE_DIR = include

# Compiler flags
CFLAGS = -m64 \
         -mcmodel=kernel \
         -mno-red-zone \
         -std=c11 \
         -ffreestanding \
         -fno-stack-protector \
         -fno-builtin \
         -nostdlib \
         -O2 \
         -g \
         -Wall \
         -Wextra \
         -I$(INCLUDE_DIR) \
         -I$(KERNEL_DIR)/core \
         -I$(KERNEL_DIR)/common \
         -I$(KERNEL_DIR)/arch/x86_64

# Assembler flags
ASFLAGS = -f elf64

# Linker flags
LDFLAGS = -m elf_x86_64 \
          -T link.ld \
          --no-warn-mismatch \
          -z max-page-size=0x1000

# Source files
KERNEL_C_SRCS = \
	$(KERNEL_DIR)/main.c \
	$(KERNEL_DIR)/common/scheduler.c \
	$(KERNEL_DIR)/common/ipc.c \
	$(KERNEL_DIR)/common/device.c \
	$(KERNEL_DIR)/common/console.c \
	$(KERNEL_DIR)/common/debug.c \
	$(KERNEL_DIR)/common/task.c \
	$(KERNEL_DIR)/arch/x86_64/interrupts.c \
	$(KERNEL_DIR)/arch/x86_64/interrupts_stub.c \
	$(KERNEL_DIR)/arch/x86_64/cpu.c \
	$(KERNEL_DIR)/arch/x86_64/memory.c \
	$(KERNEL_DIR)/arch/x86_64/boot.c

KERNEL_ASM_SRCS = \
	$(BOOT_DIR)/boot.S

# Object files
KERNEL_C_OBJS = $(KERNEL_C_SRCS:.c=.o)
KERNEL_ASM_OBJS = $(KERNEL_ASM_SRCS:.S=.o)
OBJS = $(addprefix $(BUILD_DIR)/, $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS))

# Output
KERNEL_BIN = $(BUILD_DIR)/rodnix.kernel

# Default target
.PHONY: all clean run iso

all: $(KERNEL_BIN)
	@echo "[+] Built RodNIX kernel (64-bit)"

# Link kernel
$(KERNEL_BIN): $(OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "[+] Linked kernel: $@"

# Compile C files
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "[CC] $<"

# Assemble ASM files
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@
	@echo "[AS] $<"

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) rodnix.iso
	@echo "[+] Cleaned build artifacts"

# Create ISO
iso: $(KERNEL_BIN)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/rodnix.kernel
	@echo "menuentry \"RodNIX\" {" > $(ISO_DIR)/boot/grub/grub.cfg
	@echo "    multiboot2 /boot/rodnix.kernel" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "    boot" >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo "}" >> $(ISO_DIR)/boot/grub/grub.cfg
	@if command -v grub-mkrescue >/dev/null 2>&1; then \
		grub-mkrescue -o rodnix.iso $(ISO_DIR); \
		echo "[+] Created ISO: rodnix.iso"; \
	else \
		echo "[!] grub-mkrescue not found, skipping ISO creation"; \
	fi

# Run in QEMU
run: iso
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
		qemu-system-x86_64 -m 64M -boot d -cdrom rodnix.iso -serial stdio -no-reboot -no-shutdown; \
	else \
		echo "[!] qemu-system-x86_64 not found"; \
	fi

# Debug build
debug: CFLAGS += -DDEBUG_LEVEL=DEBUG_LEVEL_DEBUG
debug: all

# Help
help:
	@echo "RodNIX Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build kernel (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  iso     - Create bootable ISO"
	@echo "  run     - Run kernel in QEMU"
	@echo "  debug   - Build with debug symbols"
	@echo "  help    - Show this help"

