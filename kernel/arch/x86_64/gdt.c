/**
 * @file gdt.c
 * @brief Minimal GDT/TSS setup for ring3 support
 */

#include "gdt.h"
#include <stdint.h>
#include "../../include/common.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t gran;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) gdt_tss_entry_t;

typedef struct {
    gdt_entry_t null;
    gdt_entry_t kcode;
    gdt_entry_t kdata;
    gdt_entry_t udata;
    gdt_entry_t ucode;
    gdt_tss_entry_t tss;
} __attribute__((packed)) gdt_table_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;

static gdt_table_t gdt;
static tss64_t tss;

static void gdt_set_entry(gdt_entry_t* e, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    e->limit_low = (uint16_t)(limit & 0xFFFF);
    e->base_low = (uint16_t)(base & 0xFFFF);
    e->base_mid = (uint8_t)((base >> 16) & 0xFF);
    e->access = access;
    e->gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    e->base_high = (uint8_t)((base >> 24) & 0xFF);
}

static void gdt_set_tss(gdt_tss_entry_t* e, uint64_t base, uint32_t limit)
{
    e->limit_low = (uint16_t)(limit & 0xFFFF);
    e->base_low = (uint16_t)(base & 0xFFFF);
    e->base_mid = (uint8_t)((base >> 16) & 0xFF);
    e->access = 0x89; /* present, type 9 (available TSS) */
    e->gran = (uint8_t)(((limit >> 16) & 0x0F));
    e->base_high = (uint8_t)((base >> 24) & 0xFF);
    e->base_upper = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    e->reserved = 0;
}

void gdt_init(void)
{
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt_set_entry(&gdt.kcode, 0, 0, 0x9A, 0xA0);
    gdt_set_entry(&gdt.kdata, 0, 0, 0x92, 0xC0);
    gdt_set_entry(&gdt.udata, 0, 0, 0xF2, 0xC0);
    gdt_set_entry(&gdt.ucode, 0, 0, 0xFA, 0xA0);

    tss.iomap_base = (uint16_t)sizeof(tss);
    gdt_set_tss(&gdt.tss, (uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);

    gdt_ptr_t gdt_ptr;
    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base = (uint64_t)(uintptr_t)&gdt;

    __asm__ volatile ("lgdt %0" : : "m"(gdt_ptr));
    __asm__ volatile (
        "movw %0, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        :
        : "r"((uint16_t)GDT_KERNEL_DS)
        : "memory", "rax"
    );

    __asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_TSS_SEL));
}

void tss_set_rsp0(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}
