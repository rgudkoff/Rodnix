# Kernel sources for the top-level Makefile.

KERNEL_C_SRCS += \
	kernel/main.c \
	init/sysinit.c \
	init/runtime.c \
	kernel/syscall.c \
	kernel/ipc.c \
	kernel/security.c \
	kernel/loader.c \
	kernel/kmod.c \
	kernel/unix/uaccess/unix_uaccess.c \
	kernel/unix/fd/unix_fd.c \
	kernel/unix/fd/file.c \
	kernel/unix/fs/unix_fs.c \
	kernel/unix/exec/unix_exec.c \
	kernel/unix/process/unix_process.c \
	kernel/unix/process/unix_proc.c \
	kernel/posix/posix_syscall.c \
	kernel/posix/posix_sys_ids.c \
	kernel/posix/posix_sys_file.c \
	kernel/posix/posix_sys_proc.c \
	kernel/posix/posix_sys_vm.c \
	kernel/posix/posix_sys_info.c \
	kernel/debug.c \
	kernel/task.c \
	kernel/kmutex.c \
	kernel/giant.c \
	mm/vm_object.c \
	mm/vm_page_ref.c \
	mm/vm_pager.c \
	mm/vm_map.c \
	mm/vm_fault.c \
	net/net.c \
	net/bsd_inet.c \
	net/bsd_ether.c \
	net/bsd_mbuf.c \
	net/bsd_ifnet.c \
	net/bsd_netisr.c \
	net/stack.c \
	net/socket.c \
	kernel/usb/usb.c \
	kernel/linux/linux_errno.c \
	kernel/linux/linux_compat.c \
	kernel/fabric/fabric.c \
	kernel/fabric/dispatcher.c \
	kernel/fabric/spin.c \
	kernel/fabric/service/net_service.c \
	kernel/fabric/service/block_service.c \
	kernel/fabric/service/platform_services.c \
	kernel/fabric/bus/virt.c \
	kernel/fabric/bus/pci.c \
	kernel/fabric/bus/usb.c \
	kernel/fabric/bus/ps2.c \
	kernel/input/input_core.c \
	drivers/fabric/usb/usb_xhci.c \
	drivers/fabric/usb/usb_host_pci.c \
	drivers/fabric/usb/usb_hid.c \
	drivers/fabric/usb/usb_msc.c \
	kernel/gfx/gfx.c \
	kernel/gfx/gfx_console.c

KERNEL_ARCH_X86_64_C_SRCS := \
	kernel/arch/x86_64/interrupts.c \
	kernel/arch/x86_64/idt.c \
	kernel/arch/x86_64/pic.c \
	kernel/arch/x86_64/lapic_access.c \
	kernel/arch/x86_64/apic.c \
	kernel/arch/x86_64/isr_handlers.c \
	kernel/arch/x86_64/cpu.c \
	kernel/arch/x86_64/cpu_topology.c \
	kernel/arch/x86_64/percpu.c \
	kernel/arch/x86_64/irqstat.c \
	kernel/arch/x86_64/smp.c \
	kernel/arch/x86_64/gdt.c \
	kernel/arch/x86_64/pmm.c \
	kernel/arch/x86_64/paging.c \
	kernel/arch/x86_64/pit.c \
	kernel/arch/x86_64/memory.c \
	kernel/arch/x86_64/boot.c \
	kernel/arch/x86_64/acpi.c \
	kernel/arch/x86_64/syscall_fast.c \
	kernel/arch/x86_64/usermode.c

KERNEL_ARCH_ARM64_C_SRCS := \
	kernel/arch/arm64/interrupts.c

KERNEL_ARCH_RISCV64_C_SRCS := \
	kernel/arch/riscv64/interrupts.c

include fs/Makefile
include lib/Makefile
include sched/Makefile
include console/Makefile
include trace/Makefile
include idl/Makefile
include shell/Makefile

KERNEL_ARCH_X86_64_ASM_SRCS := \
	kernel/arch/x86_64/isr_stubs.S \
	kernel/arch/x86_64/ap_trampoline.S \
	kernel/arch/x86_64/syscall_fast_entry.S

ifeq ($(ARCH),x86_64)
KERNEL_C_SRCS += $(KERNEL_ARCH_X86_64_C_SRCS)
KERNEL_ASM_SRCS += $(KERNEL_ARCH_X86_64_ASM_SRCS)
else ifeq ($(ARCH),arm64)
KERNEL_C_SRCS += $(KERNEL_ARCH_ARM64_C_SRCS)
else ifeq ($(ARCH),riscv64)
KERNEL_C_SRCS += $(KERNEL_ARCH_RISCV64_C_SRCS)
endif
