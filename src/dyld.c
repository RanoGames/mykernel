/* dyld.c — упрощённый динамический линкер (in-kernel)
 *
 * Поддержка:
 *  - PT_LOAD / PT_INTERP / PT_DYNAMIC
 *  - ET_EXEC и ET_DYN
 *  - DT_NEEDED → загрузка из RAM FS (/lib/...)
 *  - релокации R_386_RELATIVE, R_386_32, R_386_GLOB_DAT, R_386_JMP_SLOT
 *  - поиск символов (SysV hash + линейный обход)
 *
 * НЕ поддерживает: TLS, GNU_HASH полностью, IFUNC, версионирование glibc.
 */

#include "dyld.h"
#include "fs.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define EI_MAG0 0
#define EI_CLASS 4
#define EI_DATA  5
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define ET_DYN  3
#define EM_386  3

#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3

#define DT_NULL    0
#define DT_NEEDED  1
#define DT_PLTRELS 2
#define DT_STRTAB  5
#define DT_SYMTAB  6
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_RELAENT 9
#define DT_STRSZ  10
#define DT_SYMENT 11
#define DT_REL    17
#define DT_RELSZ  18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_JMPREL 23
#define DT_RELACOUNT 0x6ffffff9

#define STB_GLOBAL 1
#define STB_WEAK   2
#define STT_FUNC   2
#define STT_OBJECT 1
#define SHN_UNDEF  0

#define R_386_NONE     0
#define R_386_32       1
#define R_386_PC32     2
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8

#define DYLD_MAX_MODS 8
#define DYLD_LIB_BASE 0x600000u
#define DYLD_LIB_STRIDE 0x100000u
#define DYLD_MAIN_BASE 0x400000u
#define DYLD_MAIN_MAX  0x4000000u
#define DYLD_LINUX_BASE 0x08048000u
#define DYLD_LINUX_MAX  0x00800000u

struct elf32_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));

struct elf32_dyn {
    int32_t  d_tag;
    uint32_t d_val;
} __attribute__((packed));

struct elf32_sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} __attribute__((packed));

struct elf32_rel {
    uint32_t r_offset;
    uint32_t r_info;
} __attribute__((packed));

struct elf32_rela {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
} __attribute__((packed));

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((uint8_t)(i))
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)

struct module {
    int used;
    char name[48];
    uint32_t base;          /* load bias */
    const uint8_t* image;
    size_t size;
    struct elf32_sym* symtab;
    const char* strtab;
    uint32_t symcount;
    struct elf32_dyn* dynamic;
    uint32_t entry;
    int is_pie;
};

static struct module mods[DYLD_MAX_MODS];
static int mod_count;

/* встроенные blob'ы из Makefile */
extern const uint8_t _binary_build_libhello_so_start[];
extern const uint8_t _binary_build_libhello_so_end[];
extern const uint8_t _binary_build_dynhello_elf_start[];
extern const uint8_t _binary_build_dynhello_elf_end[];

static void mem_copy(void* d, const void* s, size_t n) {
    uint8_t* dd = (uint8_t*)d;
    const uint8_t* ss = (const uint8_t*)s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}
static void mem_zero(void* d, size_t n) {
    uint8_t* dd = (uint8_t*)d;
    for (size_t i = 0; i < n; i++) dd[i] = 0;
}
static int str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}
static void str_copy(char* d, const char* s, size_t n) {
    size_t i = 0;
    while (s && s[i] && i + 1 < n) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int parse_ehdr(const uint8_t* image, size_t size, const struct elf32_ehdr** eh_out) {
    if (size < sizeof(struct elf32_ehdr)) return -1;
    const struct elf32_ehdr* eh = (const struct elf32_ehdr*)image;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') return -1;
    if (eh->e_ident[EI_CLASS] != ELFCLASS32) return -1;
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB) return -1;
    if (eh->e_machine != EM_386) return -1;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return -1;
    *eh_out = eh;
    return 0;
}

static const char* find_interp(const uint8_t* image, size_t size, const struct elf32_ehdr* eh) {
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph =
            (const struct elf32_phdr*)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_INTERP) {
            if (ph->p_offset + ph->p_filesz > size) return 0;
            return (const char*)(image + ph->p_offset);
        }
    }
    return 0;
}

static struct elf32_dyn* find_dynamic(const uint8_t* image, const struct elf32_ehdr* eh, uint32_t base) {
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph =
            (const struct elf32_phdr*)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_DYNAMIC)
            return (struct elf32_dyn*)(uintptr_t)(ph->p_vaddr + base);
    }
    return 0;
}

static enum dyld_result load_segments(const uint8_t* image, size_t size,
                                     const struct elf32_ehdr* eh,
                                     uint32_t bias, int check_main_window) {
    int loaded = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph =
            (const struct elf32_phdr*)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) return DYLD_ERR_ELF;

        uint32_t addr = ph->p_vaddr + bias;
        if (check_main_window) {
            uint32_t vend = addr + ph->p_memsz;
            /* 1MB..64MB covers 0x3ff000 PHDR page + 0x400000 text */
            if (addr < 0x00100000u || vend > 0x04000000u)
                return DYLD_ERR_ELF;
        }

        uint8_t* dest = (uint8_t*)(uintptr_t)addr;
        mem_copy(dest, image + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            mem_zero(dest + ph->p_filesz, ph->p_memsz - ph->p_filesz);
        loaded = 1;
    }
    return loaded ? DYLD_OK : DYLD_ERR_ELF;
}

static void scan_dynamic(struct module* m) {
    m->symtab = 0;
    m->strtab = 0;
    m->symcount = 0;
    if (!m->dynamic) return;

    uint32_t syment = sizeof(struct elf32_sym);
    for (struct elf32_dyn* d = m->dynamic; d->d_tag != DT_NULL; d++) {
        uint32_t val = d->d_val;
        /* указатели в dynamic — va; для PIE/ET_DYN добавляем base */
        switch (d->d_tag) {
            case DT_SYMTAB:
                m->symtab = (struct elf32_sym*)(uintptr_t)(val + (m->is_pie ? m->base : 0));
                /* если уже в адресе загрузки ET_EXEC с фиксированными va */
                if (!m->is_pie) m->symtab = (struct elf32_sym*)(uintptr_t)val;
                break;
            case DT_STRTAB:
                m->strtab = (const char*)(uintptr_t)(val + (m->is_pie ? m->base : 0));
                if (!m->is_pie) m->strtab = (const char*)(uintptr_t)val;
                break;
            case DT_SYMENT:
                syment = val;
                break;
            default: break;
        }
    }
    /* оценка числа символов — до DT_STRTAB или 256 */
    m->symcount = 256;
    (void)syment;
}

static uint32_t lookup_symbol(const char* name, uint32_t* out_val) {
    for (int i = 0; i < mod_count; i++) {
        struct module* m = &mods[i];
        if (!m->symtab || !m->strtab) continue;
        for (uint32_t s = 0; s < m->symcount; s++) {
            struct elf32_sym* sym = &m->symtab[s];
            if (sym->st_name == 0) continue;
            if (sym->st_shndx == SHN_UNDEF) continue;
            const char* sn = m->strtab + sym->st_name;
            if (!str_eq(sn, name)) continue;
            int bind = ELF32_ST_BIND(sym->st_info);
            if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
            *out_val = sym->st_value + (m->is_pie ? m->base : 0);
            return 1;
        }
    }
    return 0;
}

static enum dyld_result apply_rel(struct module* m, uint32_t rel_va, uint32_t relsz, int is_rela) {
    uint32_t addr = rel_va + (m->is_pie ? m->base : 0);
    uint8_t* p = (uint8_t*)(uintptr_t)addr;
    uint8_t* end = p + relsz;

    while (p < end) {
        uint32_t r_offset, r_info;
        int32_t addend = 0;
        if (is_rela) {
            struct elf32_rela* r = (struct elf32_rela*)p;
            r_offset = r->r_offset;
            r_info = r->r_info;
            addend = r->r_addend;
            p += sizeof(struct elf32_rela);
        } else {
            struct elf32_rel* r = (struct elf32_rel*)p;
            r_offset = r->r_offset;
            r_info = r->r_info;
            p += sizeof(struct elf32_rel);
        }

        uint32_t type = ELF32_R_TYPE(r_info);
        uint32_t sym_idx = ELF32_R_SYM(r_info);
        uint32_t* where = (uint32_t*)(uintptr_t)(r_offset + (m->is_pie ? m->base : 0));

        if (!is_rela && type != R_386_RELATIVE)
            addend = (int32_t)(*where);

        uint32_t sym_val = 0;
        if (sym_idx && m->symtab && m->strtab) {
            struct elf32_sym* sym = &m->symtab[sym_idx];
            const char* sname = m->strtab + sym->st_name;
            if (sym->st_shndx == SHN_UNDEF) {
                if (!lookup_symbol(sname, &sym_val)) {
                    /* слабые символы → 0 */
                    if (ELF32_ST_BIND(sym->st_info) == STB_WEAK)
                        sym_val = 0;
                    else {
                        terminal_writestring("dyld: undefined ");
                        terminal_writestring(sname);
                        terminal_putchar('\n');
                        return DYLD_ERR_NOSYM;
                    }
                }
            } else {
                sym_val = sym->st_value + (m->is_pie ? m->base : 0);
            }
        }

        switch (type) {
            case R_386_NONE:
                break;
            case R_386_RELATIVE:
                *where = (m->base) + (uint32_t)addend;
                break;
            case R_386_32:
                *where = sym_val + (uint32_t)addend;
                break;
            case R_386_PC32:
                *where = sym_val + (uint32_t)addend - (uint32_t)(uintptr_t)where;
                break;
            case R_386_GLOB_DAT:
            case R_386_JMP_SLOT:
                *where = sym_val;
                if (type == R_386_JMP_SLOT && sym_idx && m->symtab && m->strtab) {
                    struct elf32_sym* sy = &m->symtab[sym_idx];
                    const char* nm = m->strtab + sy->st_name;
                    terminal_writestring("dyld: JMP_SLOT ");
                    terminal_writestring(nm);
                    terminal_writestring(" -> ");
                    terminal_write_hex(sym_val);
                    terminal_putchar('\n');
                }
                break;
            default:
                /* пропускаем неизвестные */
                break;
        }
    }
    return DYLD_OK;
}

static enum dyld_result relocate_module(struct module* m) {
    if (!m->dynamic) return DYLD_OK;

    uint32_t rel = 0, relsz = 0;
    uint32_t rela = 0, relasz = 0;
    uint32_t jmprel = 0, pltrelsz = 0, pltrel = DT_REL;

    for (struct elf32_dyn* d = m->dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_REL: rel = d->d_val; break;
            case DT_RELSZ: relsz = d->d_val; break;
            case DT_RELA: rela = d->d_val; break;
            case DT_RELASZ: relasz = d->d_val; break;
            case DT_JMPREL: jmprel = d->d_val; break;
            case DT_PLTRELS: pltrelsz = d->d_val; break;
            case DT_PLTREL: pltrel = d->d_val; break;
            default: break;
        }
    }

    enum dyld_result r;
    if (rel && relsz) {
        r = apply_rel(m, rel, relsz, 0);
        if (r != DYLD_OK) return r;
    }
    if (rela && relasz) {
        r = apply_rel(m, rela, relasz, 1);
        if (r != DYLD_OK) return r;
    }
    if (jmprel && pltrelsz) {
        r = apply_rel(m, jmprel, pltrelsz, pltrel == DT_RELA);
        if (r != DYLD_OK) return r;
    }
    return DYLD_OK;
}

static enum dyld_result load_one(const char* name, const uint8_t* image, size_t size,
                                uint32_t bias, int is_main, struct module** out) {
    if (mod_count >= DYLD_MAX_MODS) return DYLD_ERR_NOMEM;

    const struct elf32_ehdr* eh;
    if (parse_ehdr(image, size, &eh) != 0) return DYLD_ERR_ELF;

    int is_pie = (eh->e_type == ET_DYN);
    /* для ET_DYN shared lib: p_vaddr обычно мал, bias = load address */
    uint32_t load_bias = is_pie ? bias : 0;

    enum dyld_result dr = load_segments(image, size, eh, load_bias, is_main && !is_pie);
    if (dr != DYLD_OK) {
        /* PIE main: грузим с bias = DYLD_MAIN_BASE */
        if (is_main && is_pie) {
            load_bias = DYLD_MAIN_BASE;
            dr = load_segments(image, size, eh, load_bias, 1);
        }
        if (dr != DYLD_OK) return dr;
    }

    struct module* m = &mods[mod_count++];
    mem_zero(m, sizeof(*m));
    m->used = 1;
    str_copy(m->name, name ? name : "main", sizeof(m->name));
    m->base = load_bias;
    m->image = image;
    m->size = size;
    m->is_pie = is_pie;
    m->entry = eh->e_entry + (is_pie ? load_bias : 0);
    m->dynamic = find_dynamic(image, eh, load_bias);
    /* dual try for dynamic address */
    if (!m->dynamic) {
        for (uint16_t i = 0; i < eh->e_phnum; i++) {
            const struct elf32_phdr* ph =
                (const struct elf32_phdr*)(image + eh->e_phoff + i * eh->e_phentsize);
            if (ph->p_type == PT_DYNAMIC) {
                m->dynamic = (struct elf32_dyn*)(uintptr_t)(ph->p_vaddr + load_bias);
                break;
            }
        }
    }
    scan_dynamic(m);

    if (out) *out = m;
    return DYLD_OK;
}

static enum dyld_result load_needed(struct module* m) {
    if (!m->dynamic || !m->strtab) return DYLD_OK;

    for (struct elf32_dyn* d = m->dynamic; d->d_tag != DT_NULL; d++) {
        if (d->d_tag != DT_NEEDED) continue;
        const char* libname = m->strtab + d->d_val;
        /* basename if path-like (NEEDED often is "build/libhello.so") */
        const char* base = libname;
        for (const char* p = libname; *p; p++)
            if (*p == '/') base = p + 1;

        /* уже загружена? */
        int found = 0;
        for (int i = 0; i < mod_count; i++)
            if (str_eq(mods[i].name, base)) { found = 1; break; }
        if (found) continue;

        terminal_writestring("dyld: load ");
        terminal_writestring(base);
        terminal_putchar('\n');

        /* ищем /lib/<basename> */
        char path[64];
        path[0] = '/'; path[1] = 'l'; path[2] = 'i'; path[3] = 'b'; path[4] = '/';
        size_t i = 0;
        while (base[i] && i + 6 < sizeof(path) - 1) {
            path[5 + i] = base[i];
            i++;
        }
        path[5 + i] = '\0';

        const char* content = 0;
        size_t len = 0;
        /* Prefer embedded blob for demo lib (always consistent with kernel build) */
        if (str_eq(base, "libhello.so")) {
            extern const uint8_t _binary_build_libhello_so_start[];
            extern const uint8_t _binary_build_libhello_so_end[];
            content = (const char*)_binary_build_libhello_so_start;
            len = (size_t)(_binary_build_libhello_so_end - _binary_build_libhello_so_start);
            terminal_writestring("dyld: libhello.so from kernel blob (");
            terminal_write_uint((uint32_t)len);
            terminal_writestring(" bytes)\n");
        }
        if (!content || len == 0) {
            if (fs_read_path(path, &content, &len) != FS_OK) {
                if (fs_read_path(base, &content, &len) != FS_OK) {
                    content = 0;
                    len = 0;
                }
            }
        }
        if (!content || len == 0) {
            terminal_writestring("dyld: missing ");
            terminal_writestring(path);
            terminal_putchar('\n');
            return DYLD_ERR_NEEDED;
        }

        uint32_t bias = DYLD_LIB_BASE + (uint32_t)(mod_count > 0 ? mod_count - 1 : 0) * DYLD_LIB_STRIDE;
        enum dyld_result r = load_one(base, (const uint8_t*)content, len, bias, 0, 0);
        if (r != DYLD_OK) return r;
    }
    return DYLD_OK;
}

enum dyld_result dyld_load(const uint8_t* image, size_t size, uint32_t* entry_out) {
    mod_count = 0;
    mem_zero(mods, sizeof(mods));

    const struct elf32_ehdr* eh;
    if (parse_ehdr(image, size, &eh) != 0) return DYLD_ERR_ELF;

    const char* interp = find_interp(image, size, eh);
    if (interp) {
        terminal_writestring("dyld: PT_INTERP=");
        terminal_writestring(interp);
        terminal_writestring(" (using in-kernel dyld)\n");
    }

    struct module* main_mod = 0;
    enum dyld_result r = load_one("main", image, size, DYLD_MAIN_BASE, 1, &main_mod);
    if (r != DYLD_OK) return r;

    r = load_needed(main_mod);
    if (r != DYLD_OK) return r;

    /* релокации: сначала библиотеки, потом main */
    for (int i = 1; i < mod_count; i++) {
        r = relocate_module(&mods[i]);
        if (r != DYLD_OK) return r;
    }
    r = relocate_module(main_mod);
    if (r != DYLD_OK) return r;

    *entry_out = main_mod->entry;
    terminal_writestring("dyld: modules=");
    terminal_write_uint((uint32_t)mod_count);
    terminal_writestring(" entry=");
    terminal_write_hex(*entry_out);
    terminal_putchar('\n');
    return DYLD_OK;
}

const char* dyld_strerror(enum dyld_result r) {
    switch (r) {
        case DYLD_OK: return "OK";
        case DYLD_ERR_ELF: return "bad ELF";
        case DYLD_ERR_INTERP: return "interpreter error";
        case DYLD_ERR_NEEDED: return "missing shared library";
        case DYLD_ERR_RELOC: return "relocation failed";
        case DYLD_ERR_NOSYM: return "undefined symbol";
        case DYLD_ERR_NOMEM: return "too many modules";
        default: return "unknown";
    }
}

void dyld_install_lib_tree(void) {
    fs_mkdir_p("/lib");

    /* placeholder «интерпретатор» */
    fs_cd("/");
    fs_cd("lib");
    fs_write("ld-mykernel.so", "MyKernel in-kernel dyld (not a real userspace ld)\n");

    /* встроенная libhello.so */
    size_t lib_sz = (size_t)(_binary_build_libhello_so_end - _binary_build_libhello_so_start);
    if (lib_sz > 0 && lib_sz < FS_FILE_MAX) {
        fs_write_bin("libhello.so", _binary_build_libhello_so_start, lib_sz);
        terminal_writestring("dyld: installed /lib/libhello.so (");
        terminal_write_uint((uint32_t)lib_sz);
        terminal_writestring(" bytes)\n");
    }

    /* «libc.so.6» — заглушка-имя, реальный код = libhello для демо */
    if (lib_sz > 0 && lib_sz < FS_FILE_MAX)
        fs_write_bin("libc.so.6", _binary_build_libhello_so_start, lib_sz);

    fs_cd("/");

    /* dynhello в /bin */
    size_t dh = (size_t)(_binary_build_dynhello_elf_end - _binary_build_dynhello_elf_start);
    if (dh > 0 && dh < FS_FILE_MAX) {
        fs_cd("bin");
        fs_write_bin("dynhello", _binary_build_dynhello_elf_start, dh);
        fs_cd("/");
        terminal_writestring("dyld: installed /bin/dynhello\n");
    }
}
