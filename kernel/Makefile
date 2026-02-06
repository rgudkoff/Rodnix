# Kernel sources for the top-level Makefile.

KERNEL_C_SRCS += \
	kernel/main.c \
	kernel/common/scheduler.c \
	kernel/common/syscall.c \
	kernel/common/ipc.c \
	kernel/common/idl_runtime.c \
	kernel/common/idl_demo.c \
	kernel/common/security.c \
	kernel/common/bootstrap.c \
	kernel/common/loader.c \
	kernel/posix/posix_syscall.c \
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
	kernel/arch/x86_64/gdt.c \
	kernel/arch/x86_64/pmm.c \
	kernel/arch/x86_64/paging.c \
	kernel/arch/x86_64/pit.c \
	kernel/arch/x86_64/memory.c \
	kernel/arch/x86_64/boot.c \
	kernel/arch/x86_64/usermode.c \
	kernel/net/net.c \
	kernel/net/socket.c \
	kernel/fabric/fabric.c \
	kernel/fabric/spin.c \
	kernel/fabric/bus/virt.c \
	kernel/fabric/bus/pci.c \
	kernel/fabric/bus/ps2.c \
	kernel/input/input_core.c

include kernel/fs/Makefile

KERNEL_ASM_SRCS += \
	kernel/arch/x86_64/isr_stubs.S
