#include "idt.h"
#include "../../include/ports.h"
#include "../../include/types.h"

/* Определения архитектуры */
#ifndef ARCH_X86_64
#ifndef ARCH_I386
#ifdef __x86_64__
#define ARCH_X86_64 1
#else
#define ARCH_I386 1
#endif
#endif
#endif

#if ARCH_X86_64

/* 64-bit IDT entry (16 bytes) */
struct idt_entry64 {
    uint16_t offset_low;    /* Bits 0-15 of ISR address */
    uint16_t selector;      /* Code segment selector */
    uint8_t  ist;           /* Interrupt Stack Table index (0-7) */
    uint8_t  flags;         /* Type, DPL, Present */
    uint16_t offset_mid;    /* Bits 16-31 of ISR address */
    uint32_t offset_high;   /* Bits 32-63 of ISR address */
    uint32_t reserved;      /* Must be zero */
} __attribute__((packed));

#else /* ARCH_I386 */

/* 32-bit IDT entry (8 bytes) */
struct idt_entry32 {
    uint16_t offset_low;    /* Bits 0-15 of ISR address */
    uint16_t selector;      /* Code segment selector */
    uint8_t  zero;          /* Must be zero */
    uint8_t  flags;         /* Type, DPL, Present */
    uint16_t offset_high;   /* Bits 16-31 of ISR address */
} __attribute__((packed));

#endif /* ARCH_X86_64 */

/* IDT table */
#if ARCH_X86_64
static struct idt_entry64 idt[IDT_MAX_ENTRIES] __attribute__((aligned(16)));
#else
static struct idt_entry32 idt[IDT_MAX_ENTRIES] __attribute__((aligned(8)));
#endif

/* IDT descriptor */
struct idt_descriptor {
    uint16_t limit;
    void*    base;
} __attribute__((packed));

static struct idt_descriptor idt_desc;

void idt_init(void)
{
    /* Debug marker */
    volatile uint16_t* vga_debug = (volatile uint16_t*)0xB8000;
    vga_debug[80*2 + 0] = 0x0F49;  // 'I'
    vga_debug[80*2 + 1] = 0x0F44;  // 'D'
    vga_debug[80*2 + 2] = 0x0F54;  // 'T'
    vga_debug[80*2 + 3] = 0x0F31;  // '1'
    
    /* Debug: before loop */
    vga_debug[80*2 + 4] = 0x0F42;  // 'B' (before loop)
    
    /* Force compiler barrier */
    __asm__ volatile ("" ::: "memory");
    
    /* Debug: after barrier */
    vga_debug[80*2 + 5] = 0x0F41;  // 'A' (after barrier)
    
    /* Use direct array access - remove unnecessary pointer casts */
    vga_debug[80*2 + 6] = 0x0F50;  // 'P' (pointer cast done)
    
    /* Initialize first entry to test access */
#if ARCH_X86_64
    idt[0].offset_low = 0;
    idt[0].selector = 0x08;
    idt[0].ist = 0;
    idt[0].flags = 0;
    idt[0].offset_mid = 0;
    idt[0].offset_high = 0;
    idt[0].reserved = 0;
#else
    idt[0].offset_low = 0;
    idt[0].selector = 0x08;
    idt[0].zero = 0;
    idt[0].flags = 0;
    idt[0].offset_high = 0;
#endif
    
    vga_debug[80*2 + 7] = 0x0F31;  // '1' (first entry)
    
    /* Debug: before loop */
    vga_debug[80*2 + 8] = 0x0F4C;  // 'C' (cycle start)
    
    /* Initialize remaining entries - only initialize what we need */
    /* .bss should zero them, but we need to set selectors */
    /* Only initialize first 48 entries (32 ISR + 16 IRQ) for now */
    int i;
    for (i = 1; i < 48; i++) {  /* Only first 48 entries */
#if ARCH_X86_64
        idt[i].offset_low = 0;
        idt[i].selector = 0x08;
        idt[i].ist = 0;
        idt[i].flags = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].reserved = 0;
#else
        idt[i].offset_low = 0;
        idt[i].selector = 0x08;
        idt[i].zero = 0;
        idt[i].flags = 0;
        idt[i].offset_high = 0;
#endif
    }
    
    /* Force memory barrier after loop */
    __asm__ volatile ("" ::: "memory");
    
    vga_debug[80*2 + 9] = 0x0F4C;  // 'L' (loop done)
    
    /* Debug: immediately after loop marker */
    vga_debug[80*2 + 10] = 0x0F4D;  // 'M' (marker after loop)
    
    /* Debug: before descriptor setup */
    vga_debug[80*2 + 11] = 0x0F42;  // 'B' (before descriptor)
    
    /* Setup IDT descriptor - use explicit size calculation */
#if ARCH_X86_64
    vga_debug[80*2 + 12] = 0x0F36;  // '6' (64-bit path)
    idt_desc.limit = (IDT_MAX_ENTRIES * 16) - 1;  /* 16 bytes per entry */
#else
    vga_debug[80*2 + 12] = 0x0F33;  // '3' (32-bit path)
    idt_desc.limit = (IDT_MAX_ENTRIES * 8) - 1;   /* 8 bytes per entry */
#endif
    
    vga_debug[80*2 + 13] = 0x0F4C;  // 'L' (limit set)
    
    idt_desc.base = idt;
    
    vga_debug[80*2 + 14] = 0x0F42;  // 'B' (base set)
    
    /* Debug: after descriptor setup */
    vga_debug[80*2 + 15] = 0x0F44;  // 'D' (descriptor done)
    
    /* Debug marker - idt_init done */
    vga_debug[80*2 + 16] = 0x0F4F;  // 'O'
    vga_debug[80*2 + 17] = 0x0F4B;  // 'K'
}

void idt_set_gate(uint8_t vector, void* isr, uint8_t flags)
{
    uint64_t isr_addr = (uint64_t)isr;
    
    /* No need to check - uint8_t can't be >= 256 */
    /* if (vector >= IDT_MAX_ENTRIES) return; */
    
#if ARCH_X86_64
    idt[vector].offset_low = (uint16_t)(isr_addr & 0xFFFF);
    idt[vector].offset_mid = (uint16_t)((isr_addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((isr_addr >> 32) & 0xFFFFFFFF);
    idt[vector].selector = 0x08;  /* Kernel code segment */
    idt[vector].ist = 0;          /* Use regular stack (or set IST index) */
    idt[vector].flags = flags;
    idt[vector].reserved = 0;
#else
    idt[vector].offset_low = (uint16_t)(isr_addr & 0xFFFF);
    idt[vector].offset_high = (uint16_t)((isr_addr >> 16) & 0xFFFF);
    idt[vector].selector = 0x08;  /* Kernel code segment */
    idt[vector].zero = 0;
    idt[vector].flags = flags;
#endif
}

void idt_load(void)
{
    /* Debug marker */
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    vga[80*3 + 0] = 0x0F4C;  // 'L'
    vga[80*3 + 1] = 0x0F4F;  // 'O'
    vga[80*3 + 2] = 0x0F41;  // 'A'
    vga[80*3 + 3] = 0x0F44;  // 'D'
    
#if ARCH_X86_64
    __asm__ volatile ("lidt %0" : : "m"(idt_desc));
#else
    __asm__ volatile ("lidt %0" : : "m"(idt_desc));
#endif
    
    vga[80*3 + 4] = 0x0F4F;  // 'O'
    vga[80*3 + 5] = 0x0F4B;  // 'K'
}

void* idt_get_base(void)
{
    return idt;
}

