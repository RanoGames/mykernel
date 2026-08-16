/* dyld.c — simple ELF loader (static + basic dynamic window) */

#include "dyld.h"
#include "fs.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

#define PT_LOAD 1
#define PT_INTERP 3
#define PT_DYNAMIC 2
#define ET_EXEC 2
#define ET_DYN 3
#define EM_386 3

struct elf32_ehdr {
    uint8_t e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
} __attribute__((packed));

static void mem_copy(void* d, const void* s, size_t n) {
    uint8_t* dd = d; const uint8_t* ss = s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}
static void mem_zero(void* d, size_t n) {
    uint8_t* dd = d;
    for (size_t i = 0; i < n; i++) dd[i] = 0;
}

enum dyld_result dyld_load(const uint8_t* image, size_t size, uint32_t* entry_out) {
    if (size < sizeof(struct elf32_ehdr)) return DYLD_ERR_ELF;
    const struct elf32_ehdr* eh = (const struct elf32_ehdr*)image;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return DYLD_ERR_ELF;
    if (eh->e_ident[4] != 1) return DYLD_ERR_ELF;
    if (eh->e_machine != EM_386) return DYLD_ERR_ELF;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return DYLD_ERR_ELF;
    if (eh->e_phoff + (uint32_t)eh->e_phnum * eh->e_phentsize > size)
        return DYLD_ERR_ELF;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph =
            (const struct elf32_phdr*)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_INTERP && ph->p_offset + ph->p_filesz <= size) {
            terminal_writestring("dyld: PT_INTERP=");
            terminal_writestring((const char*)(image + ph->p_offset));
            terminal_writestring(" (in-kernel)\n");
        }
    }

    int loaded = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph =
            (const struct elf32_phdr*)(image + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) return DYLD_ERR_ELF;

        uint32_t addr = ph->p_vaddr;
        uint32_t vend = addr + ph->p_memsz;
        /* Allow 1MB..64MB (covers 0x003ff000 linker page + 0x400000 + linux base) */
        if (addr < 0x00100000u || vend > 0x04000000u)
            return DYLD_ERR_ELF;

        uint8_t* dest = (uint8_t*)(uintptr_t)addr;
        mem_copy(dest, image + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            mem_zero(dest + ph->p_filesz, ph->p_memsz - ph->p_filesz);
        loaded = 1;
    }
    if (!loaded) return DYLD_ERR_ELF;

    *entry_out = eh->e_entry;
    terminal_writestring("dyld: loaded entry=");
    terminal_write_hex(eh->e_entry);
    terminal_putchar('\n');
    return DYLD_OK;
}

const char* dyld_strerror(enum dyld_result r) {
    switch (r) {
        case DYLD_OK: return "OK";
        case DYLD_ERR_ELF: return "bad ELF / segment outside allowed load window";
        case DYLD_ERR_INTERP: return "interpreter error";
        case DYLD_ERR_NEEDED: return "missing shared library";
        case DYLD_ERR_RELOC: return "relocation failed";
        case DYLD_ERR_NOSYM: return "undefined symbol";
        case DYLD_ERR_NOMEM: return "too many modules";
        default: return "unknown";
    }
}

void dyld_install_lib_tree(void) {
    extern const uint8_t _binary_build_libhello_so_start[];
    extern const uint8_t _binary_build_libhello_so_end[];
    extern const uint8_t _binary_build_dynhello_elf_start[];
    extern const uint8_t _binary_build_dynhello_elf_end[];

    fs_mkdir_p("/lib");
    fs_cd("/");
    fs_cd("lib");
    fs_write("ld-mykernel.so", "MyKernel in-kernel dyld\n");
    size_t lib_sz = (size_t)(_binary_build_libhello_so_end - _binary_build_libhello_so_start);
    if (lib_sz > 0 && lib_sz < FS_FILE_MAX) {
        fs_write_bin("libhello.so", _binary_build_libhello_so_start, lib_sz);
        fs_write_bin("libc.so.6", _binary_build_libhello_so_start, lib_sz);
    }
    fs_cd("/");
    size_t dh = (size_t)(_binary_build_dynhello_elf_end - _binary_build_dynhello_elf_start);
    if (dh > 0 && dh < FS_FILE_MAX) {
        fs_cd("bin");
        fs_write_bin("dynhello", _binary_build_dynhello_elf_start, dh);
        fs_cd("/");
    }
}
