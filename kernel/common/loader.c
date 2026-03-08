/**
 * @file loader.c
 * @brief Minimal userland loader placeholder
 */

#include "loader.h"
#include "elf.h"
#include "../arch/x86_64/usermode.h"
#include "../arch/x86_64/paging.h"
#include "../arch/x86_64/pmm.h"
#include "../arch/x86_64/config.h"
#include "../fs/vfs.h"
#include "../core/task.h"
#include "heap.h"
#include "../../include/console.h"
#include "../../include/error.h"
#include "../../include/common.h"

#define USER_STACK_TOP 0x0000000080000000ULL
#define USER_PAGE_SIZE X86_64_PAGE_SIZE_4KB
#define LOADER_ARG_MAX 16
#define LOADER_ARG_STR_MAX 128

static inline uint64_t align_down(uint64_t v, uint64_t align)
{
    return v & ~(align - 1);
}

static inline uint64_t align_up(uint64_t v, uint64_t align)
{
    return (v + align - 1) & ~(align - 1);
}

static int loader_read_file(const char* path, uint8_t** out_buf, size_t* out_size)
{
    if (!path || !out_buf || !out_size) {
        return RDNX_E_INVALID;
    }

    vfs_file_t file;
    if (vfs_open(path, VFS_OPEN_READ, &file) != 0) {
        return RDNX_E_NOTFOUND;
    }

    size_t size_hint = 0;
    if (file.node && file.node->inode) {
        size_hint = file.node->inode->size;
    }
    if (size_hint == 0) {
        size_hint = 4096;
    }

    uint8_t* buf = (uint8_t*)kmalloc(size_hint);
    if (!buf) {
        vfs_close(&file);
        return RDNX_E_NOMEM;
    }

    size_t len = 0;
    for (;;) {
        size_t chunk = size_hint - len;
        if (chunk > 1024) {
            chunk = 1024;
        }
        if (chunk == 0) {
            break;
        }
        int r = vfs_read(&file, buf + len, chunk);
        if (r <= 0) {
            break;
        }
        len += (size_t)r;
    }

    vfs_close(&file);
    *out_buf = buf;
    *out_size = len;
    return RDNX_OK;
}

static int loader_map_segment(uint64_t pml4_phys,
                              const uint8_t* image,
                              size_t image_size,
                              const elf64_phdr_t* ph)
{
    if (!ph || !image) {
        return RDNX_E_INVALID;
    }
    if (ph->p_memsz == 0) {
        return RDNX_OK;
    }
    if (ph->p_offset + ph->p_filesz > image_size) {
        return RDNX_E_INVALID;
    }

    uint64_t seg_start = ph->p_vaddr;
    uint64_t seg_end = ph->p_vaddr + ph->p_memsz;

    uint64_t page_start = align_down(seg_start, USER_PAGE_SIZE);
    uint64_t page_end = align_up(seg_end, USER_PAGE_SIZE);

    uint64_t flags = PTE_PRESENT | PTE_USER;
    if (ph->p_flags & PF_W) {
        flags |= PTE_RW;
    }
    if ((ph->p_flags & PF_X) == 0) {
        flags |= PTE_NX;
    }

    for (uint64_t va = page_start; va < page_end; va += USER_PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page_in_zone(PMM_ZONE_NORMAL);
        if (!phys) {
            return RDNX_E_NOMEM;
        }
        if (paging_map_page_4kb_pml4(pml4_phys, va, phys, flags) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }

        uint8_t* dst = (uint8_t*)X86_64_PHYS_TO_VIRT(phys);
        memset(dst, 0, USER_PAGE_SIZE);

        if (va < seg_start) {
            uint64_t skip = seg_start - va;
            size_t avail = USER_PAGE_SIZE - (size_t)skip;
            size_t copy = (ph->p_filesz < avail) ? (size_t)ph->p_filesz : avail;
            if (copy > 0) {
                memcpy(dst + skip, image + ph->p_offset, copy);
            }
        } else {
            uint64_t seg_off = va - seg_start;
            if (seg_off < ph->p_filesz) {
                size_t copy = ph->p_filesz - seg_off;
                if (copy > USER_PAGE_SIZE) {
                    copy = USER_PAGE_SIZE;
                }
                memcpy(dst, image + ph->p_offset + seg_off, copy);
            }
        }
    }

    return RDNX_OK;
}

static int loader_map_stack(uint64_t pml4_phys, loader_image_t* out_img)
{
    if (!out_img) {
        return RDNX_E_INVALID;
    }

    out_img->stack_bottom = USER_STACK_TOP - (uint64_t)LOADER_USER_STACK_PAGES * USER_PAGE_SIZE;
    for (uint32_t i = 0; i < LOADER_USER_STACK_PAGES; i++) {
        uint64_t va = out_img->stack_bottom + (uint64_t)i * USER_PAGE_SIZE;
        uint64_t phys = pmm_alloc_page_in_zone(PMM_ZONE_NORMAL);
        if (!phys) {
            return RDNX_E_NOMEM;
        }
        uint64_t flags = PTE_PRESENT | PTE_USER | PTE_RW;
        if (paging_map_page_4kb_pml4(pml4_phys, va, phys, flags) != RDNX_OK) {
            return RDNX_E_GENERIC;
        }
        memset(X86_64_PHYS_TO_VIRT(phys), 0, USER_PAGE_SIZE);
        out_img->stack_phys[i] = phys;
    }

    out_img->user_stack = USER_STACK_TOP - 16;
    return RDNX_OK;
}

static int loader_stack_write(const loader_image_t* img, uint64_t user_va, const void* src, size_t len)
{
    if (!img || !src || len == 0) {
        return RDNX_E_INVALID;
    }

    uint64_t stack_top = USER_STACK_TOP;
    if (user_va < img->stack_bottom || user_va + len > stack_top) {
        return RDNX_E_INVALID;
    }

    const uint8_t* in = (const uint8_t*)src;
    uint64_t cur = user_va;
    size_t rem = len;
    while (rem > 0) {
        uint64_t page_off = cur - img->stack_bottom;
        uint32_t page_idx = (uint32_t)(page_off / USER_PAGE_SIZE);
        uint32_t in_page = (uint32_t)(page_off % USER_PAGE_SIZE);
        if (page_idx >= LOADER_USER_STACK_PAGES) {
            return RDNX_E_INVALID;
        }
        size_t chunk = USER_PAGE_SIZE - in_page;
        if (chunk > rem) {
            chunk = rem;
        }
        uint8_t* dst = (uint8_t*)X86_64_PHYS_TO_VIRT(img->stack_phys[page_idx]) + in_page;
        memcpy(dst, in, chunk);
        cur += chunk;
        in += chunk;
        rem -= chunk;
    }

    return RDNX_OK;
}

static int loader_prepare_user_argv(loader_image_t* img,
                                    int argc,
                                    const char* const argv[],
                                    uint64_t* out_argv_ptr)
{
    if (!img || !out_argv_ptr) {
        return RDNX_E_INVALID;
    }
    *out_argv_ptr = 0;
    if (argc <= 0 || !argv) {
        return RDNX_OK;
    }
    if (argc > LOADER_ARG_MAX) {
        argc = LOADER_ARG_MAX;
    }

    uint64_t sp = img->user_stack;
    uint64_t argv_user[LOADER_ARG_MAX];
    for (int i = argc - 1; i >= 0; i--) {
        const char* s = argv[i] ? argv[i] : "";
        size_t slen = 0;
        while (s[slen] && slen < (LOADER_ARG_STR_MAX - 1)) {
            slen++;
        }
        sp -= (uint64_t)(slen + 1);
        if (sp < img->stack_bottom) {
            return RDNX_E_NOMEM;
        }
        int wr = loader_stack_write(img, sp, s, slen + 1);
        if (wr != RDNX_OK) {
            return wr;
        }
        argv_user[i] = sp;
    }

    sp &= ~0x7ULL;
    sp -= (uint64_t)(argc + 1) * sizeof(uint64_t);
    if (sp < img->stack_bottom) {
        return RDNX_E_NOMEM;
    }
    for (int i = 0; i < argc; i++) {
        int wr = loader_stack_write(img, sp + (uint64_t)i * sizeof(uint64_t), &argv_user[i], sizeof(uint64_t));
        if (wr != RDNX_OK) {
            return wr;
        }
    }
    uint64_t null_ptr = 0;
    int wr = loader_stack_write(img, sp + (uint64_t)argc * sizeof(uint64_t), &null_ptr, sizeof(uint64_t));
    if (wr != RDNX_OK) {
        return wr;
    }

    img->user_stack = sp & ~0xFULL;
    *out_argv_ptr = sp;
    return RDNX_OK;
}

static int loader_load_elf(const uint8_t* image, size_t size, loader_image_t* out)
{
    if (!image || size < sizeof(elf64_ehdr_t) || !out) {
        return RDNX_E_INVALID;
    }

    const elf64_ehdr_t* eh = (const elf64_ehdr_t*)image;
    if (eh->e_magic != ELF_MAGIC ||
        eh->e_class != ELFCLASS64 ||
        eh->e_data != ELFDATA2LSB ||
        eh->e_type != ET_EXEC ||
        eh->e_machine != EM_X86_64) {
        return RDNX_E_INVALID;
    }
    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size) {
        return RDNX_E_INVALID;
    }

    uint64_t pml4_phys = paging_create_user_pml4();
    if (!pml4_phys) {
        return RDNX_E_NOMEM;
    }

    const elf64_phdr_t* ph = (const elf64_phdr_t*)(image + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) {
            continue;
        }
        if (ph[i].p_vaddr >= X86_64_KERNEL_VIRT_BASE) {
            return RDNX_E_INVALID;
        }
        int ret = loader_map_segment(pml4_phys, image, size, &ph[i]);
        if (ret != RDNX_OK) {
            return ret;
        }
    }

    out->pml4_phys = pml4_phys;
    out->entry = eh->e_entry;
    out->user_stack = 0;
    out->stack_bottom = 0;
    for (uint32_t i = 0; i < LOADER_USER_STACK_PAGES; i++) {
        out->stack_phys[i] = 0;
    }

    int ret = loader_map_stack(pml4_phys, out);
    if (ret != RDNX_OK) {
        return ret;
    }
    return RDNX_OK;
}

int loader_init(void)
{
    return 0;
}

int loader_load_image(const void* image, size_t size)
{
    loader_image_t img;
    int ret = loader_load_elf((const uint8_t*)image, size, &img);
    return ret;
}

int loader_enter_user_stub(void)
{
    kputs("[LOADER] enter_user_stub\n");
    void* entry = NULL;
    void* user_stack = NULL;
    uint64_t rsp0 = 0;
    if (usermode_prepare_stub(&entry, &user_stack, &rsp0) != 0) {
        kputs("[LOADER] prepare_stub failed\n");
        return RDNX_E_GENERIC;
    }
    thread_t* cur = thread_get_current();
    if (cur && cur->stack) {
        rsp0 = (uint64_t)(uintptr_t)cur->stack + cur->stack_size - 16;
    }
    if (!rsp0) {
        kputs("[LOADER] rsp0 missing\n");
        return RDNX_E_INVALID;
    }
    usermode_enter(entry, user_stack, rsp0, 0, 0);
    return 0;
}

int loader_exec(const char* path)
{
    return loader_execve(path, 0, NULL);
}

int loader_execve(const char* path, int argc, const char* const argv[])
{
    if (!path) {
        return RDNX_E_INVALID;
    }
    uint8_t* buf = NULL;
    size_t size = 0;
    int ret = loader_read_file(path, &buf, &size);
    if (ret != RDNX_OK) {
        kputs("[LOADER] file not found\n");
        return ret;
    }

    loader_image_t img;
    ret = loader_load_elf(buf, size, &img);
    kfree(buf);
    if (ret != RDNX_OK) {
        kputs("[LOADER] ELF load failed\n");
        return ret;
    }

    thread_t* cur = thread_get_current();
    uint64_t rsp0 = 0;
    if (cur && cur->stack) {
        rsp0 = (uint64_t)(uintptr_t)cur->stack + cur->stack_size - 16;
    }
    if (!rsp0) {
        return RDNX_E_INVALID;
    }

    uint64_t argv_ptr = 0;
    ret = loader_prepare_user_argv(&img, argc, argv, &argv_ptr);
    if (ret != RDNX_OK) {
        return ret;
    }

    usermode_set_pml4(img.pml4_phys);
    if (cur && cur->task) {
        cur->task->address_space = (void*)(uintptr_t)img.pml4_phys;
    }
    kputs("[LOADER] entering userland\n");
    usermode_enter((void*)(uintptr_t)img.entry,
                   (void*)(uintptr_t)img.user_stack,
                   rsp0,
                   (uint64_t)(argc > 0 ? argc : 0),
                   argv_ptr);
    return RDNX_OK;
}
