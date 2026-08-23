# Host tools.
#
# Everything the build shells out to on the *host* is named here, so that a
# machine missing one thing fails with a name you can grep for rather than a
# "command not found" in the middle of a recipe. Toolchain binaries (CC, LD,
# AS) are chosen per target and live in MakeInc.def instead.

SHELL := /bin/bash

AWK    ?= awk
PYTHON ?= python3

# ISO creation. GRUB is preferred; Limine is the fallback. Homebrew installs
# the cross GRUB under a prefixed name and outside PATH, so both are probed.
GRUB_MKRESCUE          := grub-mkrescue
GRUB_MKRESCUE_ALT      := i686-elf-grub-mkrescue
GRUB_MKRESCUE_ALT_PATH := /opt/homebrew/opt/i686-elf-grub/bin/i686-elf-grub-mkrescue
GRUB_FILE              := grub-file
GRUB_FILE_ALT          := i686-elf-grub-file
GRUB_FILE_ALT_PATH     := /opt/homebrew/opt/i686-elf-grub/bin/i686-elf-grub-file
XORRISO                := xorriso
LIMINE                 := limine
