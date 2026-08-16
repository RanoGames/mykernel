/* elf.c — загрузка ELF32 ET_EXEC в память. */

#include "elf.h"
#include <stddef.h>

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA  5

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_386 3
#define PT_LOAD 1

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

static void mem_copy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

static void mem_zero(void* dst, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = 0;
}

enum elf_result elf_load(const uint8_t* image, size_t size, uint32_t* entry) {
    if (size < sizeof(struct elf32_ehdr))
        return ELF_ERR_BAD_MAGIC;

    const struct elf32_ehdr* eh = (const struct elf32_ehdr*)image;

    if (eh->e_ident[EI_MAG0] != 0x7F ||
        eh->e_ident[EI_MAG1] != 'E' ||
        eh->e_ident[EI_MAG2] != 'L' ||
        eh->e_ident[EI_MAG3] != 'F')
        return ELF_ERR_BAD_MAGIC;

    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
        return ELF_ERR_NOT_32;
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
        return ELF_ERR_BAD_MAGIC;
    if (eh->e_type != ET_EXEC)
        return ELF_ERR_NOT_EXEC;
    if (eh->e_machine != EM_386)
        return ELF_ERR_BAD_ARCH;

    if (eh->e_phoff + (uint32_t)eh->e_phnum * eh->e_phentsize > size)
        return ELF_ERR_NO_LOAD;

    int loaded = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const struct elf32_phdr* ph =
            (const struct elf32_phdr*)(image + eh->e_phoff + i * eh->e_phentsize);

        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_offset + ph->p_filesz > size)
            return ELF_ERR_NO_LOAD;

        /* Разрешаем загрузку только в окно ELF_LOAD_BASE .. + ELF_LOAD_MAX */
        if (ph->p_vaddr < ELF_LOAD_BASE ||
            ph->p_vaddr + ph->p_memsz > ELF_LOAD_BASE + ELF_LOAD_MAX)
            return ELF_ERR_TOO_BIG;

        uint8_t* dest = (uint8_t*)(uintptr_t)ph->p_vaddr;
        mem_copy(dest, image + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            mem_zero(dest + ph->p_filesz, ph->p_memsz - ph->p_filesz);

        loaded = 1;
    }

    if (!loaded)
        return ELF_ERR_NO_LOAD;

    *entry = eh->e_entry;
    return ELF_OK;
}

const char* elf_strerror(enum elf_result r) {
    switch (r) {
        case ELF_OK: return "OK";
        case ELF_ERR_BAD_MAGIC: return "not an ELF file";
        case ELF_ERR_NOT_32: return "not ELF32";
        case ELF_ERR_NOT_EXEC: return "not an executable (ET_EXEC)";
        case ELF_ERR_BAD_ARCH: return "not i386";
        case ELF_ERR_NO_LOAD: return "no loadable segments";
        case ELF_ERR_TOO_BIG: return "segment outside allowed load window";
        default: return "unknown ELF error";
    }
}
