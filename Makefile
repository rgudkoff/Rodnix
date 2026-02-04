# ================== RodNIX Makefile (macOS/Linux friendly, 64-bit) ==================

SHELL := /bin/bash

# Toolchain (64-bit)
CC = x86_64-elf-gcc
AS = nasm
LD = x86_64-elf-ld

# Compiler flags (64-bit)
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
         -I./include \
         -I./kernel/core \
         -I./kernel/common \
         -I./kernel/arch/x86_64 \
         -I./kernel/fabric \
         -I./kernel/input

ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T link.ld --no-warn-mismatch -z max-page-size=0x1000

BUILD_DIR = build
ISO_DIR   = iso

# ===== Sources =====
KERNEL_C_SRCS = \
	kernel/main.c \
	kernel/common/scheduler.c \
	kernel/common/ipc.c \
	kernel/common/console.c \
	kernel/common/debug.c \
	kernel/common/task.c \
	kernel/common/string.c \
	kernel/common/heap.c \
	kernel/common/shell.c \
	kernel/arch/x86_64/interrupts.c \
	kernel/arch/x86_64/idt.c \
	kernel/arch/x86_64/pic.c \
	kernel/arch/x86_64/apic.c \
	kernel/arch/x86_64/isr_handlers.c \
	kernel/arch/x86_64/cpu.c \
	kernel/arch/x86_64/pmm.c \
	kernel/arch/x86_64/paging.c \
	kernel/arch/x86_64/pit.c \
	kernel/arch/x86_64/memory.c \
	kernel/arch/x86_64/boot.c \
	kernel/fabric/fabric.c \
	kernel/fabric/spin.c \
	kernel/fabric/bus/virt.c \
	kernel/fabric/bus/pci.c \
	kernel/fabric/bus/ps2.c \
	kernel/input/input_core.c \
	drivers/fabric/hid/hid_kbd.c

KERNEL_ASM_SRCS = \
	boot/boot.S \
	kernel/arch/x86_64/isr_stubs.S

KERNEL_C_OBJS   = $(KERNEL_C_SRCS:.c=.o)
KERNEL_ASM_OBJS = $(KERNEL_ASM_SRCS:.S=.o)
OBJS = $(addprefix $(BUILD_DIR)/, $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS))

KERNEL_BIN = $(BUILD_DIR)/rodnix.kernel
ISO_OUT    = rodnix.iso

UNAME_S := $(shell uname -s)

# ===== Tools =====
GRUB_MKRESCUE := grub-mkrescue
GRUB_MKRESCUE_ALT := i686-elf-grub-mkrescue
GRUB_MKRESCUE_ALT_PATH := /opt/homebrew/opt/i686-elf-grub/bin/i686-elf-grub-mkrescue
GRUB_FILE := grub-file
GRUB_FILE_ALT := i686-elf-grub-file
GRUB_FILE_ALT_PATH := /opt/homebrew/opt/i686-elf-grub/bin/i686-elf-grub-file
XORRISO := xorriso
LIMINE := limine

# QEMU accel
# По умолчанию не используем аппаратное ускорение (TCG), чтобы избежать
# проблем с недоступными ускорителями (hvf/kvm). При желании можно
# переопределить переменную QEMU_ACCEL снаружи:
#   make run QEMU_ACCEL="-accel hvf"
QEMU_ACCEL ?=

# QEMU flags: включаем APIC, используем классическую PC-машину с PS/2-клавой (i8042)
# Для стабильного поллинга по портам 0x60/0x64 используем -machine pc.
QEMU_FLAGS       = -m 64M -boot d -cdrom $(ISO_OUT) -serial stdio -no-reboot -no-shutdown \
                   -machine pc -cpu qemu64,+apic,+x2apic
QEMU_DEBUG_FLAGS = -s -S


# ===== Phony =====
.PHONY: all clean run iso debug check help check-deps

# ===== Build =====
all: $(KERNEL_BIN)
	@echo "[+] Built RodNIX kernel (64-bit)"

$(KERNEL_BIN): $(OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "[+] Linked kernel: $@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "[CC] $<"

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@if [ "$<" = "boot/boot.S" ]; then \
		$(AS) $(ASFLAGS) -Wno-zext-reloc $< -o $@; \
	else \
		$(AS) $(ASFLAGS) $< -o $@; \
	fi
	@echo "[AS] $<"

# ===== ISO =====
iso: $(KERNEL_BIN)
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/rodnix.kernel
	@GRUB_MKRESCUE_CMD=""; \
	if [ -x "$(GRUB_MKRESCUE_ALT_PATH)" ]; then \
		GRUB_MKRESCUE_CMD="$(GRUB_MKRESCUE_ALT_PATH)"; \
	elif command -v $(GRUB_MKRESCUE_ALT) >/dev/null 2>&1; then \
		GRUB_MKRESCUE_CMD="$(GRUB_MKRESCUE_ALT)"; \
	elif command -v $(GRUB_MKRESCUE) >/dev/null 2>&1; then \
		GRUB_MKRESCUE_CMD="$(GRUB_MKRESCUE)"; \
	fi; \
	if [ -n "$$GRUB_MKRESCUE_CMD" ]; then \
		mkdir -p $(ISO_DIR)/boot/grub; \
		if [ -f boot/grub/grub.cfg ]; then \
			cp boot/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg; \
		elif [ -f grub.cfg ]; then \
			cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg; \
		else \
			echo "[!] No grub.cfg found (expected boot/grub/grub.cfg or ./grub.cfg)"; exit 1; \
		fi; \
		echo "[*] Creating ISO with GRUB ($$GRUB_MKRESCUE_CMD)..."; \
		$$GRUB_MKRESCUE_CMD -o $(ISO_OUT) $(ISO_DIR) 2>/dev/null || \
		$$GRUB_MKRESCUE_CMD --compress=xz -o $(ISO_OUT) $(ISO_DIR); \
		echo "[+] Built ISO (GRUB): $(ISO_OUT)"; \
	elif command -v $(LIMINE) >/dev/null 2>&1; then \
		echo "[*] Creating ISO with Limine..."; \
		LIMINE_DIR="$$( $(LIMINE) --print-datadir )"; \
		if [ ! -f boot/limine.cfg ]; then \
			echo "[!] No limine.cfg found (expected boot/limine.cfg)"; exit 1; \
		fi; \
		cp boot/limine.cfg $(ISO_DIR)/limine.cfg; \
		mkdir -p $(ISO_DIR)/boot/limine; \
		mkdir -p $(ISO_DIR)/limine; \
		cp boot/limine.cfg $(ISO_DIR)/boot/limine.cfg; \
		cp boot/limine.cfg $(ISO_DIR)/boot/limine/limine.cfg; \
		cp boot/limine.cfg $(ISO_DIR)/limine/limine.cfg; \
		cp "$$LIMINE_DIR/limine-bios.sys" $(ISO_DIR)/; \
		cp "$$LIMINE_DIR/limine-bios-cd.bin" $(ISO_DIR)/; \
		cp "$$LIMINE_DIR/limine-uefi-cd.bin" $(ISO_DIR)/; \
		mkdir -p $(ISO_DIR)/EFI/BOOT; \
		cp "$$LIMINE_DIR/BOOTX64.EFI" $(ISO_DIR)/EFI/BOOT/; \
		cp boot/limine.cfg $(ISO_DIR)/EFI/BOOT/limine.cfg; \
		if command -v mcopy >/dev/null 2>&1; then \
			mcopy -i $(ISO_DIR)/limine-uefi-cd.bin boot/limine.cfg ::/limine.cfg; \
			mcopy -i $(ISO_DIR)/limine-uefi-cd.bin boot/limine.cfg ::/EFI/BOOT/limine.cfg; \
		fi; \
		$(XORRISO) -as mkisofs \
			-b limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table \
			--efi-boot limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label \
			-o $(ISO_OUT) $(ISO_DIR); \
		$(LIMINE) bios-install $(ISO_OUT); \
		echo "[+] Built ISO (Limine): $(ISO_OUT)"; \
	else \
		echo "[!] Neither GRUB nor Limine found for ISO creation."; exit 1; \
	fi

# ===== Run / Debug =====
run: iso
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
		echo "[*] Running QEMU $(QEMU_ACCEL), logging to boot.log"; \
		( qemu-system-x86_64 $(QEMU_FLAGS) $(QEMU_ACCEL) || \
		  qemu-system-x86_64 $(QEMU_FLAGS) ) 2>&1 | tee boot.log; \
	else \
		echo "[!] qemu-system-x86_64 not found. macOS: brew install qemu"; exit 1; \
	fi

debug: iso
	( qemu-system-x86_64 $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS) $(QEMU_ACCEL) & ) || \
	( qemu-system-x86_64 $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS) & )
	sleep 1
	@if command -v $(GRUB_FILE) >/dev/null 2>&1 && $(GRUB_FILE) --is-x86-multiboot2 $(KERNEL_BIN); then \
		echo "[OK] Multiboot2 header detected."; \
	else \
		echo "[WARN] Can't verify MB2 header (no grub-file or check failed)"; \
	fi
	@echo "[*] Connect debugger at :1234 (gdb/lldb)"

# ===== Check & Clean =====
check: $(KERNEL_BIN)
	@{ \
	  if command -v $(GRUB_FILE) >/dev/null 2>&1; then \
	    if $(GRUB_FILE) --is-x86-multiboot2 $(KERNEL_BIN); then \
	      echo "[OK] Multiboot2 header detected."; \
	    else \
	      echo "[FAIL] NO Multiboot2 header in $(KERNEL_BIN)"; exit 1; \
	    fi; \
	  else \
	    echo "[WARN] $(GRUB_FILE) not found; skip MB2 check."; \
	  fi; \
	}

clean:
	rm -rf $(BUILD_DIR)/ $(ISO_OUT) $(ISO_DIR)/
	@echo "[+] Cleaned build artifacts"

# Check dependencies
check-deps:
	@bash scripts/check-deps.sh

help:
	@echo "RodNIX Build System (64-bit)"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build kernel (default)"
	@echo "  clean       - Remove build artifacts"
	@echo "  iso         - Create bootable ISO with GRUB"
	@echo "  run         - Run kernel in QEMU"
	@echo "  debug       - Run with debugger support"
	@echo "  check       - Verify Multiboot2 header"
	@echo "  check-deps  - Check if all dependencies are installed"
	@echo "  help        - Show this help"
	@echo ""
	@echo "For installation instructions, see INSTALL.md"
