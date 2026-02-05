/**
 * @file x86_64/boot.c
 * @brief Boot implementation for x86_64
 * 
 * @note This implementation follows boot argument handling.
 */

#include "../../core/boot.h"
#include "types.h"
#include "config.h"
#include <stddef.h>

/* Multiboot2 info header */
struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

/* Multiboot2 tag structure */
struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
    /* Variable data follows */
} __attribute__((packed));

/* Multiboot2 tag types */
#define MB2_TAG_END           0
#define MB2_TAG_CMDLINE       1
#define MB2_TAG_BASIC_MEMINFO 4
#define MB2_TAG_MMAP          6

/* Multiboot2 command line tag (type 1) */
struct multiboot2_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
} __attribute__((packed));

/* Multiboot2 basic memory info tag (type 4) */
struct multiboot2_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower; /* in KB */
    uint32_t mem_upper; /* in KB */
} __attribute__((packed));

/* Multiboot2 memory map tag (type 6) */
struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* entries follow */
} __attribute__((packed));

struct multiboot2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

/* Multiboot2 memory map entry types */
#define MB2_MMAP_AVAILABLE 1

/* Use explicit initialization to ensure proper memory layout */
/* Use volatile to prevent compiler optimizations that might cause issues */
static volatile boot_info_t boot_info_storage = {0};
static volatile bool boot_info_valid = false;

static void mb2_parse_cmdline(const struct multiboot2_tag_string* tag)
{
    if (!tag) {
        return;
    }
    /* Copy into fixed buffer, ensure null termination */
    const char* src = tag->string;
    char* dst = (char*)boot_info_storage.cmdline;
    uint32_t i = 0;
    while (src[i] && i < (BOOT_LINE_LENGTH - 1)) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void mb2_parse_meminfo(const struct multiboot2_tag_basic_meminfo* tag)
{
    if (!tag) {
        return;
    }
    /* Convert from KB to bytes */
    boot_info_storage.mem_lower = (uint64_t)tag->mem_lower * 1024ULL;
    boot_info_storage.mem_upper = (uint64_t)tag->mem_upper * 1024ULL;
}

static void mb2_parse_mmap(const struct multiboot2_tag_mmap* tag)
{
    if (!tag || tag->entry_size < sizeof(struct multiboot2_mmap_entry)) {
        return;
    }
    if (tag->entry_size == 0 || tag->size < sizeof(struct multiboot2_tag_mmap)) {
        return;
    }
    /* Cache MMAP tag info for later PMM initialization */
    boot_info_storage.mmap_addr = (void*)tag;
    boot_info_storage.mmap_size = tag->size;
    boot_info_storage.mmap_entry_size = tag->entry_size;

    uint64_t max_addr = 0;
    uint64_t total_usable = 0;

    const uint8_t* base = (const uint8_t*)tag;
    uint32_t offset = sizeof(struct multiboot2_tag_mmap);
    uint32_t end = tag->size;
    uint32_t entry_size = tag->entry_size;

    if (end <= offset) {
        return;
    }

    uint32_t max_entries = (end - offset) / entry_size;
    if (max_entries > 4096) {
        max_entries = 4096;
    }

    for (uint32_t i = 0; i < max_entries; i++) {
        if (offset + entry_size > end) {
            break;
        }
        const struct multiboot2_mmap_entry* e =
            (const struct multiboot2_mmap_entry*)(base + offset);
        uint64_t addr = e->addr;
        uint64_t len = e->len;
        if (len != 0 && addr + len >= addr) {
            uint64_t end_addr = addr + len;
            if (end_addr > max_addr) {
                max_addr = end_addr;
            }
            if (e->type == MB2_MMAP_AVAILABLE) {
                uint64_t new_total = total_usable + len;
                if (new_total < total_usable) {
                    total_usable = UINT64_MAX;
                } else {
                    total_usable = new_total;
                }
            }
        }
        offset += entry_size;
    }

    if (max_addr > 0) {
        boot_info_storage.mem_upper = max_addr;
    }
    boot_info_storage.mem_lower = total_usable;
}

int boot_early_init(boot_info_t* info)
{
    if (!info) {
        return -1;
    }

    /* Copy boot information (fixed buffer for cmdline) */
    /* Use memory barriers to ensure proper ordering */
    boot_info_storage.magic = info->magic;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.boot_info = info->boot_info;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.mem_lower = info->mem_lower;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.mem_upper = info->mem_upper;
    __asm__ volatile ("" ::: "memory");
    
    boot_info_storage.flags = info->flags;
    __asm__ volatile ("" ::: "memory");

    boot_info_storage.mmap_addr = NULL;
    boot_info_storage.mmap_size = 0;
    boot_info_storage.mmap_entry_size = 0;
    __asm__ volatile ("" ::: "memory");
    
    /* Initialize cmdline buffer to empty string (fixed buffer) */
    boot_info_storage.cmdline[0] = '\0';
    __asm__ volatile ("" ::: "memory");

    /* Parse Multiboot2 info if present */
    if (boot_info_storage.magic == 0x36D76289 && boot_info_storage.boot_info) {
        const struct multiboot2_info* mbi =
            (const struct multiboot2_info*)boot_info_storage.boot_info;
        /* Sanity-check total_size to avoid walking invalid memory. */
        if (mbi->total_size < sizeof(*mbi) || mbi->total_size > (1024 * 1024)) {
            goto mb2_done;
        }
        const uint8_t* ptr = (const uint8_t*)mbi + sizeof(*mbi);
        const uint8_t* end = (const uint8_t*)mbi + mbi->total_size;

        while (ptr + sizeof(struct multiboot2_tag) <= end) {
            const struct multiboot2_tag* tag = (const struct multiboot2_tag*)ptr;
            if (tag->type == MB2_TAG_END || tag->size == 0) {
                break;
            }
            if (ptr + tag->size > end) {
                break;
            }
            switch (tag->type) {
                case MB2_TAG_CMDLINE:
                    mb2_parse_cmdline((const struct multiboot2_tag_string*)tag);
                    break;
                case MB2_TAG_BASIC_MEMINFO:
                    mb2_parse_meminfo((const struct multiboot2_tag_basic_meminfo*)tag);
                    break;
                case MB2_TAG_MMAP:
                    mb2_parse_mmap((const struct multiboot2_tag_mmap*)tag);
                    break;
                default:
                    break;
            }
            /* Tags are aligned to 8 bytes */
            uint32_t size = (tag->size + 7) & ~7U;
            if (size == 0) {
                break;
            }
            ptr += size;
        }
    }
mb2_done:
    /* Set valid flag last, with memory barrier */
    boot_info_valid = true;
    __asm__ volatile ("" ::: "memory");
    
    return 0;
}

int boot_arch_init(void)
{
    /* Initialize architecture-dependent components for x86_64 */
    /* GDT, IDT, etc. are already initialized in boot.S */
    
    return 0;
}

int boot_switch_to_64bit(void)
{
    /* Switch to 64-bit mode already done in boot.S */
    /* This function is called for compatibility but does nothing */
    
    return 0;
}

int boot_memory_init(boot_info_t* info)
{
    if (!info) {
        if (!boot_info_valid) {
            return -1;
        }
        info = (boot_info_t*)(uintptr_t)(&boot_info_storage);
    }
    
    /* Early memory initialization */
    /* PMM is already initialized */
    
    return 0;
}

int boot_interrupts_init(void)
{
    /* Early interrupt initialization */
    /* IDT is already initialized */
    
    return 0;
}

boot_info_t* boot_get_info(void)
{
    /* Use volatile read to prevent optimization issues */
    volatile bool valid = boot_info_valid;
    __asm__ volatile ("" ::: "memory");
    
    if (!valid) {
        return NULL;
    }
    
    /* Cast away volatile for return (caller knows it's safe) */
    return (boot_info_t*)&boot_info_storage;
}
