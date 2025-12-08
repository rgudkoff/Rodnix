; Multiboot2 header + начальная точка входа
; Стек на 32К, вызов kmain() в режиме flat 32-bit

MAGIC    equ 0xE85250D6
ARCH     equ 0
HDR_LEN  equ header_end - header_start
CHECKSUM equ -(MAGIC + ARCH + HDR_LEN)

        section .multiboot_header
        align 8
header_start:
        dd MAGIC
        dd ARCH
        dd HDR_LEN
        dd CHECKSUM
        ; tag_end (type=0, size=8)
        dd 0
        dd 8
header_end:

        global start
        extern kmain

        section .bss
        align 16
stack_bottom:
        resb 32768
        global stack_top
stack_top:

        section .text
start:
        cli
        mov esp, stack_top
        call kmain
hang:
        hlt
        jmp hang

