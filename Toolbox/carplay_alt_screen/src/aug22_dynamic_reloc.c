/*
 * aug22_dynamic_reloc.c - firmware-independent AUG22 libairplay GOT resolver.
 *
 * The measured K1004/P1404 direct overlays keep their exact fixed-slot path.
 * This file is linked only into the UNIVERSAL preload build.  It parses the
 * stock ELF32/ARM dynamic table from disk, derives the runtime load bias from a
 * handle-scoped exported anchor, resolves relocation slots by symbol name and
 * redirects the complete set transactionally.  No firmware-relative function
 * or GOT offset is compiled into this path.
 *
 * Every required symbol must have at least one unambiguous writable relocation,
 * every slot is validated before the first write, and a failed post-write check
 * restores every original word.  A refusal therefore leaves stock CarPlay
 * untouched; the higher-level runtime gate will not arm private type111.
 */
#include "p1404_abi.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

#ifndef RTLD_NOW
#define RTLD_NOW 2
#endif

#define ELF32_EHDR_SIZE 52u
#define ELF32_PHDR_SIZE 32u
#define ELF32_DYN_SIZE   8u
#define ELF32_SYM_SIZE   16u
#define ELF32_REL_SIZE   8u
#define ELFCLASS32       1u
#define ELFDATA2LSB      1u
#define ET_DYN           3u
#define EM_ARM           40u
#define PT_LOAD          1u
#define PT_DYNAMIC       2u
#define PF_W             2u
#define DT_NULL          0u
#define DT_PLTRELSZ      2u
#define DT_HASH          4u
#define DT_STRTAB        5u
#define DT_SYMTAB        6u
#define DT_STRSZ         10u
#define DT_SYMENT        11u
#define DT_REL           17u
#define DT_RELSZ         18u
#define DT_RELENT        19u
#define DT_PLTREL        20u
#define DT_JMPREL        23u
#define R_ARM_GLOB_DAT   21u
#define R_ARM_JUMP_SLOT  22u
#define SHN_UNDEF        0u
#define AUG22_LOAD_MAX   12u
#define AUG22_SLOT_MAX   64u
#define AUG22_SYMBOL_MAX 160u
#define AUG22_DYNSYM_MAX 65536u

struct aug22_load {
    uint32_t vaddr;
    uint32_t offset;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
};

struct aug22_elf {
    FILE *fp;
    struct aug22_load load[AUG22_LOAD_MAX];
    unsigned load_count;
    uint32_t min_vaddr;
    uint32_t max_vaddr;
    uint32_t dynamic_off;
    uint32_t dynamic_size;
    uint32_t symtab;
    uint32_t strtab;
    uint32_t strsz;
    uint32_t syment;
    uint32_t hash;
    uint32_t rel;
    uint32_t relsz;
    uint32_t relent;
    uint32_t jmprel;
    uint32_t pltrelsz;
    uint32_t pltrel;
    uint32_t nchain;
    uintptr_t base;
};

struct aug22_target {
    const char *name;
    uintptr_t target;
    unsigned found;
};

struct aug22_slot {
    volatile uintptr_t *slot;
    uintptr_t target;
    uintptr_t original;
    const char *name;
};

/* Only symbol addresses are needed here.  Raw call ABI remains owned by the
 * existing, already-validated wrappers in their original translation units. */
extern void AirPlayReceiverServerPlatformCopyProperty(void);
extern void AirPlayReceiverSessionPlatformCopyProperty(void);
extern void AirPlayReceiverSessionPlatformControl(void);
extern void AirPlayReceiverSessionSetup(void);
extern void AirPlayReceiverSessionStart(void);
extern void AirPlayReceiverSessionTearDown(void);
extern void AirPlayReceiverSessionScreen_CopyDisplaysInfo(void);
extern void ScreenCopyMain(void);
extern void ScreenStreamStart(void);
extern void ScreenStreamCreate(void);
extern void ScreenStreamProcessData(void);
extern void _ScreenStreamSetProperty(void);
extern void screen_create_window_group(void);
extern void screen_create_window_buffers(void);
extern void aug22_cscreen_config(void)
    __asm__("_ZN3dio13CScreenRender6configERKNS_16st_screen_configE");
extern void aug22_cscreen_render(void)
    __asm__("_ZN3dio13CScreenRender6renderEPh");

static volatile unsigned g_aug22_dynamic_state; /* 0 retryable, 1 installed, 2 refused */

static uint16_t rd16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_at(FILE *fp, uint32_t off, void *out, size_t n) {
    if (!fp || !out || n == 0u) return 0;
    if (fseek(fp, (long)off, SEEK_SET) != 0) return 0;
    return fread(out, 1u, n, fp) == n;
}

static int vaddr_to_file(const struct aug22_elf *e, uint32_t vaddr,
                         uint32_t bytes, uint32_t *off_out) {
    unsigned i;
    if (!e || !off_out) return 0;
    for (i = 0; i < e->load_count; ++i) {
        const struct aug22_load *l = &e->load[i];
        uint64_t start = l->vaddr;
        uint64_t end = start + l->filesz;
        uint64_t want_end = (uint64_t)vaddr + bytes;
        if ((uint64_t)vaddr >= start && want_end >= (uint64_t)vaddr && want_end <= end) {
            *off_out = l->offset + (vaddr - l->vaddr);
            return 1;
        }
    }
    return 0;
}

static int writable_runtime_slot(const struct aug22_elf *e, uint32_t vaddr) {
    unsigned i;
    if (!e || (vaddr & 3u) != 0u) return 0;
    for (i = 0; i < e->load_count; ++i) {
        const struct aug22_load *l = &e->load[i];
        uint64_t end = (uint64_t)l->vaddr + l->memsz;
        if ((l->flags & PF_W) && (uint64_t)vaddr >= l->vaddr &&
            (uint64_t)vaddr + sizeof(uintptr_t) <= end)
            return 1;
    }
    return 0;
}

static int parse_elf(FILE *fp, struct aug22_elf *e) {
    unsigned char h[ELF32_EHDR_SIZE], p[ELF32_PHDR_SIZE], d[ELF32_DYN_SIZE], hash_head[8];
    uint32_t phoff, dyn_end, off;
    uint16_t phentsize, phnum;
    unsigned i;
    if (!fp || !e) return 0;
    memset(e, 0, sizeof(*e));
    e->fp = fp;
    e->min_vaddr = 0xffffffffu;
    if (!read_at(fp, 0u, h, sizeof(h))) return 0;
    if (h[0] != 0x7fu || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
        h[4] != ELFCLASS32 || h[5] != ELFDATA2LSB ||
        rd16(h + 16) != ET_DYN || rd16(h + 18) != EM_ARM)
        return 0;
    phoff = rd32(h + 28);
    phentsize = rd16(h + 42);
    phnum = rd16(h + 44);
    if (phentsize != ELF32_PHDR_SIZE || phnum == 0u || phnum > 64u) return 0;
    for (i = 0; i < phnum; ++i) {
        uint32_t type, poff, vaddr, filesz, memsz, flags;
        if (!read_at(fp, phoff + (uint32_t)i * phentsize, p, sizeof(p))) return 0;
        type = rd32(p + 0); poff = rd32(p + 4); vaddr = rd32(p + 8);
        filesz = rd32(p + 16); memsz = rd32(p + 20); flags = rd32(p + 24);
        if (type == PT_LOAD) {
            struct aug22_load *l;
            uint64_t vend;
            if (e->load_count >= AUG22_LOAD_MAX || filesz > memsz) return 0;
            l = &e->load[e->load_count++];
            l->vaddr = vaddr; l->offset = poff; l->filesz = filesz;
            l->memsz = memsz; l->flags = flags;
            if (vaddr < e->min_vaddr) e->min_vaddr = vaddr;
            vend = (uint64_t)vaddr + memsz;
            if (vend > 0xffffffffu || (uint32_t)vend > e->max_vaddr)
                e->max_vaddr = (uint32_t)vend;
        } else if (type == PT_DYNAMIC) {
            if (e->dynamic_size != 0u) return 0;
            e->dynamic_off = poff;
            e->dynamic_size = filesz;
        }
    }
    if (!e->load_count || e->min_vaddr == 0xffffffffu ||
        e->max_vaddr <= e->min_vaddr || e->dynamic_size < ELF32_DYN_SIZE ||
        (e->dynamic_size % ELF32_DYN_SIZE) != 0u)
        return 0;
    dyn_end = e->dynamic_off + e->dynamic_size;
    if (dyn_end < e->dynamic_off) return 0;
    for (off = e->dynamic_off; off + ELF32_DYN_SIZE <= dyn_end; off += ELF32_DYN_SIZE) {
        uint32_t tag, value;
        if (!read_at(fp, off, d, sizeof(d))) return 0;
        tag = rd32(d); value = rd32(d + 4);
        if (tag == DT_NULL) break;
        switch (tag) {
            case DT_HASH: e->hash = value; break;
            case DT_STRTAB: e->strtab = value; break;
            case DT_SYMTAB: e->symtab = value; break;
            case DT_STRSZ: e->strsz = value; break;
            case DT_SYMENT: e->syment = value; break;
            case DT_REL: e->rel = value; break;
            case DT_RELSZ: e->relsz = value; break;
            case DT_RELENT: e->relent = value; break;
            case DT_JMPREL: e->jmprel = value; break;
            case DT_PLTRELSZ: e->pltrelsz = value; break;
            case DT_PLTREL: e->pltrel = value; break;
            default: break;
        }
    }
    if (!e->hash || !e->strtab || !e->symtab || !e->strsz ||
        e->syment != ELF32_SYM_SIZE || e->relent != ELF32_REL_SIZE)
        return 0;
    if (!vaddr_to_file(e, e->hash, sizeof(hash_head), &off) ||
        !read_at(fp, off, hash_head, sizeof(hash_head)))
        return 0;
    e->nchain = rd32(hash_head + 4);
    if (e->nchain == 0u || e->nchain > AUG22_DYNSYM_MAX) return 0;
    if (e->rel && (e->relsz % ELF32_REL_SIZE) != 0u) return 0;
    if (e->jmprel && (e->pltrelsz % ELF32_REL_SIZE) != 0u) return 0;
    if (e->jmprel && e->pltrel != DT_REL) return 0;
    return 1;
}

static int read_symbol(const struct aug22_elf *e, uint32_t index,
                       uint32_t *name_off, uint32_t *value, uint16_t *shndx) {
    unsigned char s[ELF32_SYM_SIZE];
    uint32_t file_off;
    if (!e || index >= e->nchain ||
        !vaddr_to_file(e, e->symtab + index * ELF32_SYM_SIZE,
                       ELF32_SYM_SIZE, &file_off) ||
        !read_at(e->fp, file_off, s, sizeof(s)))
        return 0;
    if (name_off) *name_off = rd32(s + 0);
    if (value) *value = rd32(s + 4);
    if (shndx) *shndx = rd16(s + 14);
    return 1;
}

static int read_symbol_name(const struct aug22_elf *e, uint32_t name_off,
                            char out[AUG22_SYMBOL_MAX]) {
    uint32_t str_file, i;
    int ch;
    if (!e || !out || name_off >= e->strsz ||
        !vaddr_to_file(e, e->strtab + name_off, 1u, &str_file))
        return 0;
    if (fseek(e->fp, (long)str_file, SEEK_SET) != 0) return 0;
    for (i = 0; i + 1u < AUG22_SYMBOL_MAX && name_off + i < e->strsz; ++i) {
        ch = fgetc(e->fp);
        if (ch == EOF) return 0;
        out[i] = (char)ch;
        if (ch == 0) return 1;
    }
    out[AUG22_SYMBOL_MAX - 1u] = 0;
    return 0;
}

static int find_symbol(const struct aug22_elf *e, const char *wanted,
                       uint32_t *value_out, uint16_t *shndx_out) {
    uint32_t i;
    if (!e || !wanted) return 0;
    for (i = 0; i < e->nchain; ++i) {
        uint32_t noff, value;
        uint16_t shndx;
        char name[AUG22_SYMBOL_MAX];
        if (!read_symbol(e, i, &noff, &value, &shndx)) return 0;
        if (!noff) continue;
        if (!read_symbol_name(e, noff, name)) return 0;
        if (!strcmp(name, wanted)) {
            if (value_out) *value_out = value;
            if (shndx_out) *shndx_out = shndx;
            return 1;
        }
    }
    return 0;
}

static int target_index(struct aug22_target *targets, unsigned target_count,
                        const char *name) {
    unsigned i;
    for (i = 0; i < target_count; ++i)
        if (!strcmp(targets[i].name, name)) return (int)i;
    return -1;
}

static int collect_rel_range(struct aug22_elf *e, void *stock_handle,
                             uint32_t rel_vaddr, uint32_t rel_size,
                             struct aug22_target *targets, unsigned target_count,
                             struct aug22_slot *slots, unsigned *slot_count) {
    uint32_t rel_file, count, i;
    if (!rel_vaddr || !rel_size) return 1;
    if (!vaddr_to_file(e, rel_vaddr, rel_size, &rel_file)) return 0;
    count = rel_size / ELF32_REL_SIZE;
    for (i = 0; i < count; ++i) {
        unsigned char r[ELF32_REL_SIZE];
        uint32_t r_offset, r_info, sym_index, type, name_off;
        uint16_t shndx;
        char name[AUG22_SYMBOL_MAX];
        int ti;
        volatile uintptr_t *slot;
        uintptr_t current, expected = 0u;
        if (!read_at(e->fp, rel_file + i * ELF32_REL_SIZE, r, sizeof(r))) return 0;
        r_offset = rd32(r); r_info = rd32(r + 4);
        type = r_info & 0xffu;
        if (type != R_ARM_GLOB_DAT && type != R_ARM_JUMP_SLOT) continue;
        sym_index = r_info >> 8;
        if (!read_symbol(e, sym_index, &name_off, NULL, &shndx) ||
            !name_off || !read_symbol_name(e, name_off, name))
            return 0;
        ti = target_index(targets, target_count, name);
        if (ti < 0) continue;
        if (!writable_runtime_slot(e, r_offset) || *slot_count >= AUG22_SLOT_MAX)
            return 0;
        slot = (volatile uintptr_t *)(e->base + r_offset);
        current = *slot;
        expected = (uintptr_t)dlsym(stock_handle, name);
        if (!expected || expected == targets[ti].target) {
            void *next = dlsym(RTLD_NEXT, name);
            if (next && (uintptr_t)next != targets[ti].target) expected = (uintptr_t)next;
        }
        if (current != targets[ti].target && current != expected &&
            !(current >= e->base + e->min_vaddr && current < e->base + e->max_vaddr))
            return 0;
        slots[*slot_count].slot = slot;
        slots[*slot_count].target = targets[ti].target;
        slots[*slot_count].original = current;
        slots[*slot_count].name = targets[ti].name;
        ++*slot_count;
        ++targets[ti].found;
        (void)shndx;
    }
    return 1;
}

static int resolve_base(struct aug22_elf *e, void *stock_handle) {
    uint32_t value;
    uint16_t shndx;
    void *runtime;
    if (!e || !stock_handle ||
        !find_symbol(e, "AirPlayReceiverSessionSetup", &value, &shndx) ||
        shndx == SHN_UNDEF || value == 0u)
        return 0;
    runtime = dlsym(stock_handle, "AirPlayReceiverSessionSetup");
    if (!runtime || (uintptr_t)runtime < value) return 0;
    e->base = (uintptr_t)runtime - value;
    if ((uintptr_t)runtime < e->base + e->min_vaddr ||
        (uintptr_t)runtime >= e->base + e->max_vaddr)
        return 0;
    return 1;
}

static int try_install(void) {
    static const char *const paths[] = {
        P1404_LIBAIRPLAY_PATH_APP,
        P1404_LIBAIRPLAY_PATH_ESO,
        NULL
    };
    struct aug22_target targets[] = {
        { "AirPlayReceiverServerPlatformCopyProperty", (uintptr_t)&AirPlayReceiverServerPlatformCopyProperty, 0u },
        { "ScreenStreamStart", (uintptr_t)&ScreenStreamStart, 0u },
        { "AirPlayReceiverSessionSetup", (uintptr_t)&AirPlayReceiverSessionSetup, 0u },
        { "AirPlayReceiverSessionTearDown", (uintptr_t)&AirPlayReceiverSessionTearDown, 0u },
        { "AirPlayReceiverSessionStart", (uintptr_t)&AirPlayReceiverSessionStart, 0u },
        { "AirPlayReceiverSessionPlatformCopyProperty", (uintptr_t)&AirPlayReceiverSessionPlatformCopyProperty, 0u },
        { "AirPlayReceiverSessionPlatformControl", (uintptr_t)&AirPlayReceiverSessionPlatformControl, 0u },
        { "_ZN3dio13CScreenRender6renderEPh", (uintptr_t)&aug22_cscreen_render, 0u },
        { "ScreenCopyMain", (uintptr_t)&ScreenCopyMain, 0u },
        { "ScreenStreamProcessData", (uintptr_t)&ScreenStreamProcessData, 0u },
        { "_ScreenStreamSetProperty", (uintptr_t)&_ScreenStreamSetProperty, 0u },
        { "AirPlayReceiverSessionScreen_CopyDisplaysInfo", (uintptr_t)&AirPlayReceiverSessionScreen_CopyDisplaysInfo, 0u },
        { "ScreenStreamCreate", (uintptr_t)&ScreenStreamCreate, 0u },
        { "_ZN3dio13CScreenRender6configERKNS_16st_screen_configE", (uintptr_t)&aug22_cscreen_config, 0u },
        { "screen_create_window_group", (uintptr_t)&screen_create_window_group, 0u },
        { "screen_create_window_buffers", (uintptr_t)&screen_create_window_buffers, 0u }
    };
    struct aug22_slot slots[AUG22_SLOT_MAX];
    struct aug22_elf elf;
    const char *path = NULL;
    FILE *fp = NULL;
    void *handle = NULL;
    unsigned slot_count = 0u, target_count = sizeof(targets) / sizeof(targets[0]);
    unsigned i;
    int ok = 0;

    for (i = 0; paths[i]; ++i) {
        if (p1404_read_marker(paths[i])) { path = paths[i]; break; }
    }
    /* A missing stock path during early constructor order is retryable. */
    if (!path) return 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    handle = dlopen(path, RTLD_NOW);
    if (!handle) { fclose(fp); return 0; }

    if (!parse_elf(fp, &elf) || !resolve_base(&elf, handle)) goto structural_refuse;
    if (elf.rel && !collect_rel_range(&elf, handle, elf.rel, elf.relsz,
                                      targets, target_count, slots, &slot_count))
        goto structural_refuse;
    if (elf.jmprel && (elf.jmprel != elf.rel || elf.pltrelsz != elf.relsz) &&
        !collect_rel_range(&elf, handle, elf.jmprel, elf.pltrelsz,
                           targets, target_count, slots, &slot_count))
        goto structural_refuse;
    if (!slot_count) goto structural_refuse;
    for (i = 0; i < target_count; ++i) {
        /*
         * _ScreenStreamSetProperty is present on the measured P1404/K1004
         * stock images and is required for complete codec-config evidence.
         * Keep it optional on unknown AUG22 siblings so lack of one internal
         * relocation never disables the already-proven private111 chain.
         */
        if (targets[i].found == 0u &&
            strcmp(targets[i].name, "_ScreenStreamSetProperty") != 0)
            goto structural_refuse;
    }

    /* Complete validation above, first mutation below. */
    for (i = 0; i < slot_count; ++i) *slots[i].slot = slots[i].target;
    __sync_synchronize();
    for (i = 0; i < slot_count; ++i) {
        if (*slots[i].slot != slots[i].target) {
            unsigned j;
            for (j = 0; j < slot_count; ++j) *slots[j].slot = slots[j].original;
            __sync_synchronize();
            goto structural_refuse;
        }
    }
    ok = 1;
    fclose(fp);
    __sync_lock_test_and_set(&g_aug22_dynamic_state, 1u);
    return ok;

structural_refuse:
    if (fp) fclose(fp);
    __sync_lock_test_and_set(&g_aug22_dynamic_state, 2u);
    return 0;
}

int aug22_dynamic_install_internal_redirects(void) {
    unsigned state = __sync_fetch_and_add(&g_aug22_dynamic_state, 0u);
    if (state == 1u) return 1;
    if (state == 2u) return 0;
    return try_install();
}

int aug22_dynamic_internal_redirects_ready(void) {
    unsigned state = __sync_fetch_and_add(&g_aug22_dynamic_state, 0u);
    if (state == 1u) return 1;
    if (state == 2u) return 0;
    /* Retry once from the asynchronous runtime worker after loader constructors. */
    return aug22_dynamic_install_internal_redirects();
}
