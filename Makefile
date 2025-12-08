# ================== RodNIX Makefile (macOS/Linux friendly) ==================

CC = i686-elf-gcc
AS = nasm
LD = i686-elf-ld

CFLAGS  = -m32 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib -O2 -g -Wall -Wextra -I./include
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T link.ld

BUILD_DIR = build
ISO_DIR   = iso

# Минимальная сборка - загрузка, GDT, IDT, PIC, IRQ, PIT
KERNEL_C_SRCS = \
	kernel/main.c \
	kernel/gdt.c \
	kernel/idt.c \
	kernel/isr.c \
	kernel/pic.c \
	kernel/pit.c \
	kernel/device.c \
	kernel/vfs.c \
	kernel/pmm.c \
	kernel/paging.c \
	kernel/vmm.c \
	kernel/heap.c \
	kernel/driver.c \
	drivers/console.c \
	drivers/ports.c \
	drivers/ata_driver.c

KERNEL_ASM_SRCS = \
	boot/boot.S \
	boot/gdt_flush.S \
	boot/idt_load.S \
	boot/isr_stubs.S

KERNEL_C_OBJS   = $(KERNEL_C_SRCS:.c=.o)
KERNEL_ASM_OBJS = $(KERNEL_ASM_SRCS:.S=.o)
OBJS = $(addprefix $(BUILD_DIR)/, $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS))

KERNEL_BIN = $(BUILD_DIR)/rodnix.kernel

UNAME_S := $(shell uname -s)

# ---- Autodetect tools (supports macOS cross packages) ----------------------
# Try native grub-mkrescue, then i686-elf-*, then x86_64-elf-* (Homebrew tap)
GRUB_MKRESCUE ?= $(shell command -v grub-mkrescue 2>/dev/null || true)
ifeq ($(GRUB_MKRESCUE),)
  GRUB_MKRESCUE := $(shell command -v i686-elf-grub-mkrescue 2>/dev/null || true)
endif
ifeq ($(GRUB_MKRESCUE),)
  GRUB_MKRESCUE := $(shell command -v x86_64-elf-grub-mkrescue 2>/dev/null || true)
endif

XORRISO ?= $(shell command -v xorriso 2>/dev/null || true)
MTOOLS  ?= $(shell command -v mtools 2>/dev/null || true)

# Try to guess GRUB modules directory (used on macOS Homebrew cross build)
# Typically: <brew_prefix>/Cellar/i686-elf-grub/*/lib/i686-elf/grub/i386-pc
GRUB_MODDIR ?= $(shell d="$$(dirname $$(dirname $(GRUB_MKRESCUE)))"/lib/grub/i386-pc; [ -d "$$d" ] && echo "$$d" || true)
ifeq ($(GRUB_MODDIR),)
  GRUB_MODDIR := $(shell d="$$(dirname $$(dirname $(GRUB_MKRESCUE)))"/lib/i686-elf/grub/i386-pc; [ -d "$$d" ] && echo "$$d" || true)
endif

define need_tools
	@{ ok=1; \
	  if [ -z "$(GRUB_MKRESCUE)" ]; then \
	    echo "Error: grub-mkrescue not found."; ok=0; \
	    if [ "$(UNAME_S)" = "Darwin" ]; then \
	      echo "  macOS hint: brew tap nativeos/i386-elf-toolchain && brew install i686-elf-grub"; \
	    else \
	      echo "  Linux hint: sudo apt-get install grub-pc-bin grub-common"; \
	    fi; \
	  fi; \
	  if [ -z "$(XORRISO)" ]; then \
	    echo "Error: xorriso not found."; ok=0; \
	    if [ "$(UNAME_S)" = "Darwin" ]; then echo "  macOS: brew install xorriso"; \
	    else echo "  Linux: sudo apt-get install xorriso"; fi; \
	  fi; \
	  if [ $$ok -eq 0 ]; then exit 1; fi; }
endef

# ---- Debugger & QEMU flags -------------------------------------------------
# Debugger autodetect: i686-elf-gdb -> gdb -> lldb
DEBUGGER ?= $(shell command -v i686-elf-gdb 2>/dev/null || command -v gdb 2>/dev/null || command -v lldb 2>/dev/null)

QEMU_FLAGS       = -m 64M -boot d -cdrom rodnix.iso -serial stdio -no-reboot -no-shutdown
QEMU_DEBUG_FLAGS = -s -S

.PHONY: all clean run iso debug check docker-iso

all: $(KERNEL_BIN)
	@echo "[+] Built RodNIX kernel"

$(KERNEL_BIN): $(OBJS) link.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

iso: $(KERNEL_BIN)
	$(call need_tools)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/rodnix.kernel
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	@if [ -n "$(GRUB_MODDIR)" ]; then \
	  echo "[*] Using GRUB modules at: $(GRUB_MODDIR)"; \
	  "$(GRUB_MKRESCUE)" -d "$(GRUB_MODDIR)" -o rodnix.iso $(ISO_DIR); \
	else \
	  "$(GRUB_MKRESCUE)" -o rodnix.iso $(ISO_DIR); \
	fi
	@echo "[+] Built ISO: rodnix.iso"

run: iso
	# Try HVF (macOS); on failure fallback to default (TCG)
	qemu-system-i386 $(QEMU_FLAGS) -accel hvf || \
	qemu-system-i386 $(QEMU_FLAGS)

debug: iso
	# Start QEMU waiting for debugger on :1234; try HVF, else fallback
	( qemu-system-i386 $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS) -accel hvf & ) || \
	( qemu-system-i386 $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS) & )
	# Give QEMU time to open the port
	sleep 1
	# Attach debugger (GDB preferred; LLDB fallback)
	@if echo "$(DEBUGGER)" | grep -q lldb; then \
	  echo "[*] Using LLDB"; \
	  lldb -o "target create $(KERNEL_BIN)" -o "gdb-remote 1234"; \
	else \
	  echo "[*] Using GDB: $(DEBUGGER)"; \
	  $(DEBUGGER) -ex "target remote :1234" -ex "symbol-file $(KERNEL_BIN)"; \
	fi

check: $(KERNEL_BIN)
	@{ \
	  if command -v grub-file >/dev/null 2>&1; then \
	    if grub-file --is-x86-multiboot2 $(KERNEL_BIN); then \
	      echo "[OK] Multiboot2 header detected."; \
	    else \
	      echo "[FAIL] NO Multiboot2 header in $(KERNEL_BIN)"; exit 1; \
	    fi; \
	  else \
	    echo "[WARN] grub-file not found; skip MB2 check."; \
	  fi; \
	}

docker-iso: clean
	# Portable build inside Ubuntu container (no Homebrew needed)
	docker run --rm -v "$(PWD)":/w -w /w ubuntu:24.04 bash -lc "\
	  apt-get update && \
	  apt-get install -y build-essential nasm xorriso mtools grub-pc-bin grub-common && \
	  make iso \
	"

clean:
	rm -rf $(BUILD_DIR)/ rodnix.iso $(ISO_DIR)/
