#include "kmod.h"

#include "../../include/common.h"
#include "../../include/error.h"
#include "../fs/vfs.h"
#include "elf.h"
#include "heap.h"

typedef struct {
    int used;
    kmod_info_t info;
    void* image_mem;
    uint64_t image_mem_size;
    int (*mod_init)(void);
    void (*mod_fini)(void);
} kmod_slot_t;

static kmod_slot_t g_kmods[KMOD_MAX];
static uint32_t g_kmod_count = 0;
static int g_kmod_inited = 0;

static void kmod_copy_text(char* dst, uint32_t cap, const char* src)
{
    if (!dst || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
}

static int kmod_find_slot(const char* name)
{
    if (!name || !name[0]) {
        return -1;
    }
    for (uint32_t i = 0; i < g_kmod_count; i++) {
        if (g_kmods[i].used && strcmp(g_kmods[i].info.name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int kmod_add_slot(const char* name,
                         const char* kind,
                         const char* version,
                         uint32_t flags,
                         uint8_t builtin,
                         uint8_t loaded,
                         void* image_mem,
                         uint64_t image_mem_size,
                         int (*mod_init)(void),
                         void (*mod_fini)(void))
{
    if (g_kmod_count >= KMOD_MAX) {
        return RDNX_E_BUSY;
    }
    kmod_slot_t* slot = &g_kmods[g_kmod_count++];
    slot->used = 1;
    memset(&slot->info, 0, sizeof(slot->info));
    kmod_copy_text(slot->info.name, sizeof(slot->info.name), name);
    kmod_copy_text(slot->info.kind, sizeof(slot->info.kind), kind ? kind : "misc");
    kmod_copy_text(slot->info.version, sizeof(slot->info.version), version ? version : "0");
    slot->info.flags = flags;
    slot->info.builtin = builtin;
    slot->info.loaded = loaded;
    slot->image_mem = image_mem;
    slot->image_mem_size = image_mem_size;
    slot->mod_init = mod_init;
    slot->mod_fini = mod_fini;
    return RDNX_OK;
}

static int kmod_validate_header(const kmod_image_header_t* hdr)
{
    if (!hdr) {
        return RDNX_E_INVALID;
    }
    if (memcmp(hdr->magic, KMOD_IMAGE_MAGIC, KMOD_IMAGE_MAGIC_LEN) != 0) {
        return RDNX_E_INVALID;
    }
    if (hdr->name[0] == '\0') {
        return RDNX_E_INVALID;
    }
    return RDNX_OK;
}

static int kmod_parse_elf_module(const uint8_t* image,
                                 uint64_t size,
                                 kmod_image_header_t* out_hdr,
                                 void** out_image_mem,
                                 uint64_t* out_image_mem_size,
                                 int (**out_init)(void),
                                 void (**out_fini)(void))
{
    if (!image || !out_hdr || !out_image_mem || !out_image_mem_size || !out_init || !out_fini) {
        return RDNX_E_INVALID;
    }
    *out_image_mem = NULL;
    *out_image_mem_size = 0;
    *out_init = NULL;
    *out_fini = NULL;

    const elf64_ehdr_t* eh = (const elf64_ehdr_t*)image;
    if (eh->e_magic != ELF_MAGIC ||
        eh->e_class != ELFCLASS64 ||
        eh->e_data != ELFDATA2LSB ||
        eh->e_type != ET_REL) {
        return RDNX_E_INVALID;
    }
    if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shentsize != sizeof(elf64_shdr_t)) {
        return RDNX_E_INVALID;
    }
    if (eh->e_shstrndx >= eh->e_shnum) {
        return RDNX_E_INVALID;
    }

    uint64_t sht_end = eh->e_shoff + ((uint64_t)eh->e_shnum * sizeof(elf64_shdr_t));
    if (sht_end > size) {
        return RDNX_E_INVALID;
    }
    const elf64_shdr_t* sh = (const elf64_shdr_t*)(image + eh->e_shoff);
    const elf64_shdr_t* shstr = &sh[eh->e_shstrndx];
    if (shstr->sh_type != SHT_STRTAB ||
        shstr->sh_offset >= size ||
        (shstr->sh_offset + shstr->sh_size) > size) {
        return RDNX_E_INVALID;
    }
    const char* shstrtab = (const char*)(image + shstr->sh_offset);

    int mod_sec = -1;
    int sym_sec = -1;
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        const elf64_shdr_t* sec = &sh[i];
        if (sec->sh_name < shstr->sh_size) {
            const char* sec_name = shstrtab + sec->sh_name;
            if (strcmp(sec_name, ".rodnix_mod") == 0) {
                mod_sec = (int)i;
            }
        }
        if (sec->sh_type == SHT_SYMTAB) {
            sym_sec = (int)i;
        }
    }
    if (mod_sec < 0) {
        return RDNX_E_NOTFOUND;
    }

    const elf64_shdr_t* ms = &sh[mod_sec];
    if (ms->sh_offset >= size ||
        (ms->sh_offset + ms->sh_size) > size ||
        ms->sh_size < sizeof(kmod_image_header_t)) {
        return RDNX_E_INVALID;
    }
    memcpy(out_hdr, image + ms->sh_offset, sizeof(kmod_image_header_t));
    int rc = kmod_validate_header(out_hdr);
    if (rc != RDNX_OK) {
        return rc;
    }

    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        const elf64_shdr_t* sec = &sh[i];
        if ((sec->sh_type == SHT_RELA || sec->sh_type == SHT_REL) &&
            sec->sh_size != 0 &&
            sec->sh_info < eh->e_shnum &&
            (sh[sec->sh_info].sh_flags & SHF_ALLOC)) {
            return RDNX_E_UNSUPPORTED;
        }
    }

    uint64_t* sec_runtime = (uint64_t*)kmalloc((uint64_t)eh->e_shnum * sizeof(uint64_t));
    if (!sec_runtime) {
        return RDNX_E_NOMEM;
    }
    memset(sec_runtime, 0, (uint64_t)eh->e_shnum * sizeof(uint64_t));

    uint64_t total = 0;
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        const elf64_shdr_t* sec = &sh[i];
        if ((sec->sh_flags & SHF_ALLOC) == 0 || sec->sh_size == 0) {
            continue;
        }
        uint64_t align = sec->sh_addralign ? sec->sh_addralign : 1u;
        uint64_t aligned = (total + align - 1u) & ~(align - 1u);
        if (aligned < total) {
            kfree(sec_runtime);
            return RDNX_E_INVALID;
        }
        total = aligned + sec->sh_size;
        if (total > (16u * 1024u * 1024u)) {
            kfree(sec_runtime);
            return RDNX_E_NOMEM;
        }
    }

    void* image_mem = NULL;
    if (total > 0) {
        image_mem = kmalloc((size_t)total);
        if (!image_mem) {
            kfree(sec_runtime);
            return RDNX_E_NOMEM;
        }
        memset(image_mem, 0, (size_t)total);
    }

    uint64_t cursor = 0;
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        const elf64_shdr_t* sec = &sh[i];
        if ((sec->sh_flags & SHF_ALLOC) == 0 || sec->sh_size == 0) {
            continue;
        }
        uint64_t align = sec->sh_addralign ? sec->sh_addralign : 1u;
        cursor = (cursor + align - 1u) & ~(align - 1u);
        sec_runtime[i] = (uint64_t)(uintptr_t)image_mem + cursor;
        if (sec->sh_type != SHT_NOBITS) {
            if (sec->sh_offset >= size || (sec->sh_offset + sec->sh_size) > size) {
                if (image_mem) {
                    kfree(image_mem);
                }
                kfree(sec_runtime);
                return RDNX_E_INVALID;
            }
            memcpy((void*)(uintptr_t)sec_runtime[i], image + sec->sh_offset, (size_t)sec->sh_size);
        }
        cursor += sec->sh_size;
    }

    if (sym_sec >= 0) {
        const elf64_shdr_t* ss = &sh[sym_sec];
        if (ss->sh_entsize != sizeof(elf64_sym_t) ||
            ss->sh_offset >= size ||
            (ss->sh_offset + ss->sh_size) > size ||
            ss->sh_link >= eh->e_shnum) {
            if (image_mem) {
                kfree(image_mem);
            }
            kfree(sec_runtime);
            return RDNX_E_INVALID;
        }
        const elf64_shdr_t* strsec = &sh[ss->sh_link];
        if (strsec->sh_type != SHT_STRTAB ||
            strsec->sh_offset >= size ||
            (strsec->sh_offset + strsec->sh_size) > size) {
            if (image_mem) {
                kfree(image_mem);
            }
            kfree(sec_runtime);
            return RDNX_E_INVALID;
        }

        const elf64_sym_t* syms = (const elf64_sym_t*)(image + ss->sh_offset);
        uint64_t sym_count = ss->sh_size / sizeof(elf64_sym_t);
        const char* strtab = (const char*)(image + strsec->sh_offset);
        for (uint64_t i = 0; i < sym_count; i++) {
            const elf64_sym_t* sym = &syms[i];
            if (sym->st_name >= strsec->sh_size) {
                continue;
            }
            if (sym->st_shndx == SHN_UNDEF || sym->st_shndx >= eh->e_shnum) {
                continue;
            }
            if (sec_runtime[sym->st_shndx] == 0) {
                continue;
            }
            const char* nm = strtab + sym->st_name;
            uint64_t addr = sec_runtime[sym->st_shndx] + sym->st_value;
            if (strcmp(nm, "rodnix_kmod_init") == 0) {
                *out_init = (int (*)(void))(uintptr_t)addr;
            } else if (strcmp(nm, "rodnix_kmod_fini") == 0) {
                *out_fini = (void (*)(void))(uintptr_t)addr;
            }
        }
    }

    kfree(sec_runtime);
    *out_image_mem = image_mem;
    *out_image_mem_size = total;
    return RDNX_OK;
}

int kmod_init(void)
{
    for (uint32_t i = 0; i < KMOD_MAX; i++) {
        g_kmods[i].used = 0;
        memset(&g_kmods[i].info, 0, sizeof(g_kmods[i].info));
        g_kmods[i].image_mem = NULL;
        g_kmods[i].image_mem_size = 0;
        g_kmods[i].mod_init = NULL;
        g_kmods[i].mod_fini = NULL;
    }
    g_kmod_count = 0;
    g_kmod_inited = 1;
    return RDNX_OK;
}

int kmod_register_builtin(const char* name, const char* kind, const char* version, uint32_t flags)
{
    if (!g_kmod_inited || !name || !name[0]) {
        return RDNX_E_INVALID;
    }

    int idx = kmod_find_slot(name);
    if (idx >= 0) {
        g_kmods[idx].info.builtin = 1u;
        g_kmods[idx].info.loaded = 1u;
        return RDNX_OK;
    }
    return kmod_add_slot(name, kind, version, flags, 1u, 1u, NULL, 0, NULL, NULL);
}

int kmod_get_info(uint32_t index, kmod_info_t* out)
{
    if (!out || index >= g_kmod_count || !g_kmods[index].used) {
        return RDNX_E_NOTFOUND;
    }
    *out = g_kmods[index].info;
    return RDNX_OK;
}

uint32_t kmod_count(void)
{
    return g_kmod_count;
}

int kmod_load(const char* path)
{
    if (!g_kmod_inited || !path || !path[0]) {
        return RDNX_E_INVALID;
    }

    vfs_stat_t st;
    int rc = vfs_stat(path, &st);
    if (rc != RDNX_OK || st.size == 0 || st.size > (1024u * 1024u)) {
        return (rc == RDNX_OK) ? RDNX_E_INVALID : rc;
    }

    uint8_t* image = (uint8_t*)kmalloc((size_t)st.size);
    if (!image) {
        return RDNX_E_NOMEM;
    }

    vfs_file_t f;
    rc = vfs_open(path, VFS_OPEN_READ, &f);
    if (rc != RDNX_OK) {
        kfree(image);
        return rc;
    }
    int n = vfs_read(&f, image, (size_t)st.size);
    (void)vfs_close(&f);
    if (n <= 0) {
        kfree(image);
        return RDNX_E_INVALID;
    }

    kmod_image_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    void* mod_image_mem = NULL;
    uint64_t mod_image_mem_size = 0;
    int (*mod_init)(void) = NULL;
    void (*mod_fini)(void) = NULL;
    if ((uint64_t)n >= sizeof(kmod_image_header_t) &&
        memcmp(image, KMOD_IMAGE_MAGIC, KMOD_IMAGE_MAGIC_LEN) == 0) {
        memcpy(&hdr, image, sizeof(hdr));
        rc = kmod_validate_header(&hdr);
    } else {
        rc = kmod_parse_elf_module(image,
                                   (uint64_t)n,
                                   &hdr,
                                   &mod_image_mem,
                                   &mod_image_mem_size,
                                   &mod_init,
                                   &mod_fini);
    }
    kfree(image);
    if (rc != RDNX_OK) {
        return rc;
    }

    int idx = kmod_find_slot(hdr.name);
    if (idx >= 0) {
        if (mod_image_mem) {
            kfree(mod_image_mem);
        }
        return RDNX_E_BUSY;
    }

    if (mod_init) {
        int init_rc = mod_init();
        if (init_rc != 0) {
            if (mod_image_mem) {
                kfree(mod_image_mem);
            }
            return RDNX_E_GENERIC;
        }
    }

    return kmod_add_slot(hdr.name,
                         hdr.kind,
                         hdr.version,
                         hdr.flags,
                         0u,
                         1u,
                         mod_image_mem,
                         mod_image_mem_size,
                         mod_init,
                         mod_fini);
}

int kmod_unload(const char* name)
{
    int idx = kmod_find_slot(name);
    if (idx < 0) {
        return RDNX_E_NOTFOUND;
    }
    if (g_kmods[idx].info.builtin) {
        return RDNX_E_DENIED;
    }
    if (!g_kmods[idx].info.loaded) {
        return RDNX_E_INVALID;
    }
    if (g_kmods[idx].mod_fini) {
        g_kmods[idx].mod_fini();
    }
    if (g_kmods[idx].image_mem) {
        kfree(g_kmods[idx].image_mem);
        g_kmods[idx].image_mem = NULL;
        g_kmods[idx].image_mem_size = 0;
    }
    g_kmods[idx].mod_init = NULL;
    g_kmods[idx].mod_fini = NULL;
    g_kmods[idx].info.loaded = 0u;
    return RDNX_OK;
}
