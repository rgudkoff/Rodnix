# ================== RodNIX Makefile (cross-host, multi-arch) ==================
#
# The build is split the way XNU's is, so that each question has one place to
# be answered:
#
#   makedefs/MakeInc.cmd    host tools
#   makedefs/MakeInc.def    target, toolchain, flags        -- how it is built
#   makedefs/MakeInc.dir    the component list              -- what exists
#   makedefs/MakeInc.rule   compile and link rules
#   config/MASTER[.<arch>]  enabled options                 -- what it is
#   <component>/conf/files  that component's sources
#
# This file is left with the things that are neither: the artifacts (ISO,
# initrd, USB image), running under QEMU, and the CI smokes.

.DEFAULT_GOAL := all

include makedefs/MakeInc.cmd
include makedefs/MakeInc.def
include makedefs/MakeInc.dir
include makedefs/MakeInc.rule

UNAME_S := $(shell uname -s)


# QEMU accel
# Use software emulation by default to avoid unavailable host accelerators.
# Override QEMU_ACCEL externally when acceleration is known to be available:
#   make run QEMU_ACCEL="-accel hvf"
QEMU_ACCEL ?=

# QEMU serial backend:
# - Use mon:stdio by default for interactive terminal I/O.
# - For file-backed logs, set QEMU_SERIAL="file:boot.log".
QEMU_SERIAL ?= mon:stdio
# QEMU NIC for first real Fabric backend (e1000).
QEMU_NET_FLAGS ?= -netdev user,id=net0 -device e1000,netdev=net0
QEMU_CPU ?= qemu64,+apic,+x2apic
QEMU_SMP ?= 1
QEMU_DISK_IMG ?= $(BUILD_DIR)/rodnix-disk.img
QEMU_DISK_SIZE_MB ?= 128
QEMU_DISK_FS_STAMP ?= $(BUILD_DIR)/rodnix-disk.ext2.stamp
TCC_AUTO_FLAG ?= $(USERLAND_DIR)/rootfs/etc/tcc.auto
RUN_DISK_TARGET := $(if $(wildcard $(TCC_AUTO_FLAG)),tcc-disk,qemu-disk)
#
# QEMU flags: enable APIC and keep the legacy PS/2 controller path available.
# Use -machine pc for stable polling on ports 0x60/0x64.
QEMU_FLAGS       = -m 1G -boot d -cdrom $(ISO_OUT) -serial $(QEMU_SERIAL) -no-reboot -no-shutdown \
                   -drive file=$(QEMU_DISK_IMG),if=ide,format=raw,index=0,media=disk \
                   -machine pc -smp $(QEMU_SMP) -cpu $(QEMU_CPU) $(QEMU_NET_FLAGS)
QEMU_DEBUG_FLAGS = -s -S

IDL_OUT ?= $(BUILD_DIR)/idl
IDL_INPUT ?= scripts/idl/example.defs


# ===== Phony =====
# Component targets (kernel, mm, sched, ...) are declared phony in
# makedefs/MakeInc.dir, next to the list that defines them.
.PHONY: all clean run run-verbose _run_impl iso debug gdb check check-abi \
        sync-bsd-abi help check-deps idl-headers idl-copy userland initrd \
        posix-syscalls check-contract contract-smoke check-contract-10 \
        check-ifconfig-smoke check-smp-topology check-tcc-smoke qemu-disk \
        tcc tcc-disk tcc-smoke usb-image usb-flash

# ===== Build =====
all: check-abi posix-syscalls $(KERNEL_BIN)
	@echo "[+] Built RodNIX kernel (64-bit)"

# ===== ISO =====
iso: $(KERNEL_BIN) initrd
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/rodnix.kernel
	@if [ -f $(INITRD_IMG) ]; then \
		cp $(INITRD_IMG) $(ISO_DIR)/boot/initrd.img; \
	fi
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
			if [ -n "$(KERNEL_CMDLINE)" ]; then \
				sed -E "s|^[[:space:]]*multiboot2[[:space:]]+/boot/rodnix\\.kernel.*$$|    multiboot2 /boot/rodnix.kernel $(KERNEL_CMDLINE)|" \
					$(ISO_DIR)/boot/grub/grub.cfg > $(ISO_DIR)/boot/grub/grub.cfg.tmp; \
				mv $(ISO_DIR)/boot/grub/grub.cfg.tmp $(ISO_DIR)/boot/grub/grub.cfg; \
				echo "[*] GRUB cmdline: $(KERNEL_CMDLINE)"; \
			fi; \
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
run: $(RUN_DISK_TARGET)
	@if [ "$(V)" = "1" ] || [ "$(VERBOSE)" = "1" ]; then \
		$(MAKE) --no-print-directory _run_impl KERNEL_CMDLINE="bootverbose verbose_sysinit=1 startup_debug=verbose bootlog=verbose"; \
	else \
		$(MAKE) --no-print-directory _run_impl; \
	fi

run-verbose: $(RUN_DISK_TARGET)
	@$(MAKE) --no-print-directory _run_impl KERNEL_CMDLINE="bootverbose verbose_sysinit=1 startup_debug=verbose bootlog=verbose"

_run_impl: iso $(RUN_DISK_TARGET)
	@if command -v $(QEMU_SYSTEM) >/dev/null 2>&1; then \
		echo "[*] Running QEMU $(QEMU_ACCEL), serial=$(QEMU_SERIAL), tee -> boot.log"; \
		( $(QEMU_SYSTEM) $(QEMU_FLAGS) $(QEMU_ACCEL) || \
		  $(QEMU_SYSTEM) $(QEMU_FLAGS) ) 2>&1 | tee boot.log; \
	else \
		echo "[!] $(QEMU_SYSTEM) not found. Install QEMU with your host package manager."; exit 1; \
	fi

debug:
	@if [ -n "$(filter run,$(MAKECMDGOALS))" ]; then \
		echo "[*] debug flag enabled for run (verbose boot logs)"; \
	else \
		$(MAKE) --no-print-directory _run_impl KERNEL_CMDLINE="bootverbose verbose_sysinit=1 startup_debug=verbose bootlog=verbose"; \
	fi

gdb: iso $(RUN_DISK_TARGET)
	( $(QEMU_SYSTEM) $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS) $(QEMU_ACCEL) & ) || \
	( $(QEMU_SYSTEM) $(QEMU_FLAGS) $(QEMU_DEBUG_FLAGS) & )
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
	rm -rf $(BUILD_DIR)/ $(ISO_DIR)/
	@echo "[+] Cleaned build artifacts"

# Check dependencies
check-deps:
	@bash scripts/check-deps.sh

check-abi:
	@python3 scripts/check_bsd_abi_headers.py

sync-bsd-abi:
	@python3 scripts/sync_bsd_abi_headers.py

check-contract contract-smoke:
	@bash scripts/ci/contract_qemu.sh

check-contract-10:
	@for i in $$(seq 1 10); do \
		echo "[contract] run $$i/10"; \
		bash scripts/ci/contract_qemu.sh || exit 1; \
	done

check-ifconfig-smoke:
	@bash scripts/ci/smoke_ifconfig_qemu.sh

check-smp-topology:
	@bash scripts/ci/smoke_smp_topology.sh

check-tcc-smoke tcc-smoke:
	@bash scripts/ci/smoke_tcc_qemu.sh

qemu-disk:
	@mkdir -p $(dir $(QEMU_DISK_IMG))
	@if [ ! -f "$(QEMU_DISK_IMG)" ]; then \
		echo "[*] Creating QEMU disk image: $(QEMU_DISK_IMG) ($(QEMU_DISK_SIZE_MB) MiB)"; \
		dd if=/dev/zero of="$(QEMU_DISK_IMG)" bs=1m count="$(QEMU_DISK_SIZE_MB)" status=none; \
	fi
	@if [ ! -f "$(QEMU_DISK_FS_STAMP)" ]; then \
		echo "[*] Formatting demo ext2 filesystem on $(QEMU_DISK_IMG)"; \
		python3 scripts/mkext2_demo.py --output "$(QEMU_DISK_IMG)" --size-mb "$(QEMU_DISK_SIZE_MB)"; \
		touch "$(QEMU_DISK_FS_STAMP)"; \
	fi

TCC_STAGING = $(BUILD_DIR)/tcc-staging
TCC_STAMP   = $(BUILD_DIR)/tcc.stamp
TCC_DEPS    = scripts/build_tcc.sh userland/Makefile $(shell find userland/include userland/libc -type f | sort)

# Build TCC cross-compiled for RodNIX and stage it.
tcc: $(TCC_STAMP)

$(TCC_STAMP): $(TCC_DEPS)
	@ARCH=$(ARCH) bash scripts/build_tcc.sh
	@touch $(TCC_STAMP)

# Rebuild the ext2 disk image, injecting the TCC staging tree.
# Use a separate stamp so qemu-disk's stamp doesn't block re-injection.
TCC_DISK_STAMP = $(BUILD_DIR)/rodnix-disk-tcc.stamp

tcc-disk: $(TCC_STAMP)
	@mkdir -p $(dir $(QEMU_DISK_IMG))
	@echo "[*] (Re)creating disk image with TCC: $(QEMU_DISK_IMG)"
	@python3 scripts/mkext2_demo.py \
		--output "$(QEMU_DISK_IMG)" \
		--size-mb "$(QEMU_DISK_SIZE_MB)" \
		--inject-dir "$(TCC_STAGING)"
	@touch "$(QEMU_DISK_FS_STAMP)"
	@touch "$(TCC_DISK_STAMP)"
	@echo "[+] Disk image ready with TCC at /usr/bin/tcc"

USB_IMG      ?= $(BUILD_DIR)/rodnix-usb.img
USB_SIZE_MB  ?= 512
USB_FAT_MB   ?= 64
USB_DEV      ?=

usb-image: $(KERNEL_BIN) initrd
	@python3 scripts/mkusb_image.py \
		--kernel "$(KERNEL_BIN)" \
		--initrd "$(INITRD_IMG)" \
		--output "$(USB_IMG)" \
		--size-mb "$(USB_SIZE_MB)" \
		--fat-mb  "$(USB_FAT_MB)"

usb-flash: usb-image
	@if [ -z "$(USB_DEV)" ]; then \
		echo "[!] Set USB_DEV to the whole target disk (example: make usb-flash USB_DEV=/dev/disk4)"; \
		exit 1; \
	fi
	@bash scripts/flash_usb.sh "$(USB_IMG)" "$(USB_DEV)"

# Code generation, as distinct from building the idl component. Before the
# component targets existed, this was called "idl"; it now has a name that
# says which of the two it is.
idl-headers:
	@mkdir -p $(IDL_OUT)
	@$(PYTHON) scripts/idl/idlgen.py $(IDL_INPUT) $(IDL_OUT)

idl-copy:
	@mkdir -p include/idl
	@cp -f $(IDL_OUT)/*.h include/idl/

help:
	@echo "RodNIX Build System (64-bit)"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build kernel (default)"
	@echo "  <component> - Build one component's objects; see below"
	@echo "  clean       - Remove build artifacts"
	@echo "  idl-headers - Generate IDL headers"
	@echo "  userland    - Build userland binaries"
	@echo "  initrd      - Build initrd image"
	@echo "  iso         - Create bootable ISO with GRUB"
	@echo "  usb-image   - Create bootable USB disk image (MBR+FAT32+ext2)"
	@echo "  usb-flash   - Build USB image and write it to USB_DEV"
	@echo "  run         - Run kernel in QEMU (quiet)"
	@echo "  run V=1     - Run kernel in QEMU (verbose boot)"
	@echo "  run-verbose - Same as 'run V=1'"
	@echo "  debug       - Run with verbose kernel diagnostics"
	@echo "  gdb         - Run paused for debugger (:1234)"
	@echo "  check       - Verify Multiboot2 header"
	@echo "  check-abi   - Verify userland BSD ABI constants"
	@echo "  check-contract - Run contract CI smoke in QEMU"
	@echo "  contract-smoke - Alias for check-contract"
	@echo "  check-contract-10 - Run contract smoke 10 times"
	@echo "  check-ifconfig-smoke - Run ifconfig smoke scenario in QEMU"
	@echo "  check-smp-topology - Verify MADT CPU inventory against -smp 1/2/4/8"
	@echo "  tcc-smoke    - Boot with injected TCC and compile a smoke object in QEMU"
	@echo "  sync-bsd-abi - Sync userland ABI headers from the vendor snapshot"
	@echo "  tcc         - Cross-compile TCC for RodNIX (needs x86_64-elf-gcc)"
	@echo "  tcc-disk    - Rebuild ext2 disk image with TCC injected"
	@echo "  check-deps  - Check if all dependencies are installed"
	@echo "  help        - Show this help"
	@echo ""
	@echo "QEMU overrides:"
	@echo "  QEMU_CPU=max   - Override the guest CPU model/features"
	@echo "  QEMU_SMP=2     - Run with more than one virtual CPU (experimental)"
	@echo ""
	@echo "Architecture overrides:"
	@echo "  ARCH=x86_64    - Active target"
	@echo "  ARCH=arm64     - Bootstrap scaffolding only"
	@echo "  ARCH=riscv64   - Bootstrap scaffolding only"
	@echo "  TOOLCHAIN=gcc  - Default cross-GCC flow"
	@echo "  TOOLCHAIN=clang - Use clang + lld with --target=<triple>"
	@echo ""
	@echo "Components (each builds on its own, e.g. 'make mm'):"
	@echo "  $(COMPONENTS)"
	@echo ""
	@echo "  Sources        - <component>/conf/files[.<arch>]"
	@echo "  Options        - config/MASTER[.<arch>]"
	@echo "  Flags & rules  - makedefs/MakeInc.*"
	@echo ""
	@echo "Artifact layout:"
	@echo "  Kernel image   - $(BUILD_ROOT)/<arch>/rodnix.kernel"
	@echo "  ISO image      - $(BUILD_ROOT)/<arch>/rodnix.iso"
	@echo "  ISO staging    - $(ISO_ROOT)/<arch>/"
	@echo ""
	@echo "For installation instructions, see INSTALL.md"

userland: posix-syscalls
	@$(MAKE) -C $(USERLAND_DIR) ARCH=$(ARCH) TOOLCHAIN=$(TOOLCHAIN)

initrd: userland scripts/mkinitrd.py
	@mkdir -p $(dir $(INITRD_IMG))
	@$(PYTHON) scripts/mkinitrd.py $(USERLAND_ROOTFS) $(INITRD_IMG)

posix-syscalls: scripts/mkposixsyscalls.py kernel/posix/syscalls.master
	@python3 scripts/mkposixsyscalls.py .

-include $(DEPS)
# Ensure generated POSIX syscall tables exist before compiling C/ASM objects.
$(OBJS): | posix-syscalls
