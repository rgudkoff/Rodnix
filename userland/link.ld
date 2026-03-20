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

    /* C++ global constructors / destructors */
    .init_array : {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    }

    .fini_array : {
        __fini_array_start = .;
        KEEP(*(.fini_array*))
        __fini_array_end = .;
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
