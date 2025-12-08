; Загрузка GDT и переключение сегментов
global gdt_flush
extern gdt_ptr

gdt_flush:
    lgdt [gdt_ptr]
    mov ax, 0x10      ; Селектор данных (0x10 = 2-й дескриптор, RPL=0)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush   ; Селектор кода (0x08 = 1-й дескриптор, RPL=0)
.flush:
    ret

