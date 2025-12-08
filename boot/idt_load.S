global idt_load

; void idt_load(uint32_t idt_ptr);
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

