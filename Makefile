CC = i686-elf-gcc
AS = nasm
LD = i686-elf-ld

CFLAGS  = -m32 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra -I./include
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T link.ld

BUILD_DIR = build
ISO_DIR   = iso

KERNEL_C_SRCS = \
	kernel/main.c \
	kernel/gdt.c \
	kernel/idt.c \
	kernel/isr.c \
	kernel/irq.c \
	kernel/pic.c \
	kernel/pit.c \
	kernel/keyboard.c \
	drivers/console.c \
	drivers/ports.c

KERNEL_ASM_SRCS = \
	boot/boot.S \
	boot/gdt_flush.S \
	boot/idt_load.S \
	boot/isr_stubs.S

KERNEL_C_OBJS   = $(KERNEL_C_SRCS:.c=.o)
KERNEL_ASM_OBJS = $(KERNEL_ASM_SRCS:.S=.o)
OBJS = $(addprefix $(BUILD_DIR)/, $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS))

KERNEL_BIN = $(BUILD_DIR)/rodnix.kernel

.PHONY: all clean run iso debug

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
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/rodnix.kernel
	cp grub.cfg $(ISO_DIR)/boot/grub/
	grub-mkrescue -o rodnix.iso $(ISO_DIR) 2>/dev/null || \
	(echo "Error: Install grub-pc-bin and xorriso"; exit 1)

run: iso
	qemu-system-i386 -cdrom rodnix.iso -serial stdio

debug: iso
	qemu-system-i386 -cdrom rodnix.iso -s -S &
	sleep 1
	gdb $(KERNEL_BIN)

clean:
	rm -rf build/ rodnix.iso $(ISO_DIR)/