global gdt_flush

; void gdt_flush(uint32_t gdt_ptr);
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, 0x10        ; data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:flush_cs   ; far jump to reload CS
flush_cs:
    ret

