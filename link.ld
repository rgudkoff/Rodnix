/**
 * @file link.ld
 * @brief Linker script for RodNIX kernel
 */

ENTRY(start)

SECTIONS
{
    /* Kernel starts at 1MB */
    . = 0x100000;

    /* Multiboot2 header must be in first 32KB */
    .multiboot : {
        *(.multiboot_header)
    }

    /* Text section */
    .text : ALIGN(4K) {
        *(.text.start)
        *(.text)
        *(.text.*)
    }

    /* Read-only data */
    .rodata : ALIGN(4K) {
        *(.rodata)
        *(.rodata.*)
    }

    /* Data section */
    .data : ALIGN(4K) {
        *(.data)
        *(.data.*)
    }

    /* BSS section */
    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* End of kernel */
    kernel_end = .;
}

