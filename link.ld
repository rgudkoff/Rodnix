/**
 * @file link.ld
 * @brief Linker script for RodNIX kernel
 */

ENTRY(start)

SECTIONS
{
    /* Load address for boot + kernel image */
    . = 0x100000;

    /* Multiboot2 header must be in first 32KB */
    .multiboot : {
        *(.multiboot_header)
    }

    /* Boot/early code (must remain in low memory) */
    .text.start : ALIGN(4K) {
        *(.text.start)
        *(.text64)
    }

    .boot.rodata : ALIGN(4K) {
        *(.boot.rodata)
    }

    .boot.bss (NOLOAD) : ALIGN(4K) {
        *(.boot.bss)
    }

    /* Record physical start for the high-half kernel image */
    kernel_phys_start = .;

    /* Higher-half direct-map base */
    KERNEL_VMA_BASE = 0xFFFFFFFF80000000;
    KERNEL_VMA = KERNEL_VMA_BASE + kernel_phys_start;

    /* Place kernel sections at high virtual addresses (direct-map) */
    . = KERNEL_VMA;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA_BASE) {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA_BASE) {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA_BASE) {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA_BASE) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* End of kernel (virtual) */
    kernel_end = .;
}
