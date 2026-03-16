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

    __kernel_start = .;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA_BASE) {
        __text_start = .;
        *(.text)
        *(.text.*)
        __text_end = .;
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA_BASE) {
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        __rodata_end = .;
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA_BASE) {
        __data_start = .;
        *(.data)
        *(.data.*)
        __data_end = .;
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA_BASE) {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }

    .init.text ALIGN(4K) : AT(ADDR(.init.text) - KERNEL_VMA_BASE) {
        __init_start = .;
        *(.init.text)
        *(.init.text.*)
    }
    .init.rodata ALIGN(4K) : AT(ADDR(.init.rodata) - KERNEL_VMA_BASE) {
        *(.init.rodata)
        *(.init.rodata.*)
        __init_end = .;
    }

    /* End of kernel (virtual) */
    kernel_end = .;
    __kernel_end = .;
}
