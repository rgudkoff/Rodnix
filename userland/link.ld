ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    /* C++ global constructors (.init_array — new ABI). */
    .init_array : {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    }

    /* C++ global constructors (.ctors — old ELF ABI used by x86_64-elf-g++).
     * crt0.S walks this section in reverse order (end→start). */
    .ctors : {
        __ctors_start = .;
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
        __ctors_end = .;
    }

    /* C++ global destructors. */
    .fini_array : {
        __fini_array_start = .;
        KEEP(*(.fini_array*))
        __fini_array_end = .;
    }

    .dtors : {
        __dtors_start = .;
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        __dtors_end = .;
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
