# Kernel sources for the top-level Makefile.

KERNEL_C_SRCS += \
	kernel/main.c \
	kernel/common/scheduler/state.c \
	kernel/common/scheduler/runqueue.c \
	kernel/common/scheduler/control.c \
	kernel/common/scheduler/tick.c \
	kernel/common/scheduler/switch.c \
	kernel/common/scheduler/reaper.c \
	kernel/common/scheduler/debug.c \
	kernel/common/syscall.c \
	kernel/common/ipc.c \
	kernel/common/idl_runtime.c \
	kernel/common/idl_demo.c \
	kernel/common/security.c \
	kernel/common/bootstrap.c \
	kernel/common/loader.c \
	kernel/common/kmod.c \
	kernel/common/bootlog.c \
	kernel/common/tracev2.c \
	kernel/common/tty_console.c \
	kernel/common/waitq.c \
	kernel/unix/uaccess/unix_uaccess.c \
	kernel/unix/fd/unix_fd.c \
	kernel/unix/fs/unix_fs.c \
	kernel/unix/exec/unix_exec.c \
	kernel/unix/process/unix_process.c \
	kernel/posix/posix_syscall.c \
	kernel/common/console.c \
	kernel/common/debug.c \
	kernel/common/task.c \
	kernel/vm/vm_object.c \
	kernel/vm/vm_pager.c \
	kernel/vm/vm_map.c \
	kernel/vm/vm_fault.c \
	kernel/common/string.c \
	kernel/common/heap.c \
	kernel/common/shell.c \
	kernel/arch/x86_64/interrupts.c \
	kernel/arch/x86_64/idt.c \
	kernel/arch/x86_64/pic.c \
	kernel/arch/x86_64/lapic_access.c \
	kernel/arch/x86_64/apic.c \
	kernel/arch/x86_64/isr_handlers.c \
	kernel/arch/x86_64/cpu.c \
	kernel/arch/x86_64/gdt.c \
	kernel/arch/x86_64/pmm.c \
	kernel/arch/x86_64/paging.c \
	kernel/arch/x86_64/pit.c \
	kernel/arch/x86_64/memory.c \
	kernel/arch/x86_64/boot.c \
	kernel/arch/x86_64/syscall_fast.c \
	kernel/arch/x86_64/usermode.c \
	kernel/net/net.c \
	kernel/net/socket.c \
	kernel/fabric/fabric.c \
	kernel/fabric/spin.c \
	kernel/fabric/service/net_service.c \
	kernel/fabric/service/block_service.c \
	kernel/fabric/service/platform_services.c \
	kernel/fabric/bus/virt.c \
	kernel/fabric/bus/pci.c \
	kernel/fabric/bus/ps2.c \
	kernel/input/input_core.c

include kernel/fs/Makefile

KERNEL_ASM_SRCS += \
	kernel/arch/x86_64/isr_stubs.S \
	kernel/arch/x86_64/syscall_fast_entry.S
