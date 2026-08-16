/* dyld.c restored - use push from local file */
#include "dyld.h"
#include "fs.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

/* TEMP: redirect to force user curl restore if this is still stub-sized */
#if 0
#endif

enum dyld_result dyld_load(const uint8_t* image, size_t size, uint32_t* entry_out);

/* Full implementation follows in next commit if size limits hit */

#define DYLD_MAIN_BASE 0x400000u
#define DYLD_MAIN_MAX  0x4000000u
#define DYLD_LINUX_BASE 0x08048000u
#define DYLD_LINUX_MAX  0x00800000u

/* Minimal loader: copy PT_LOAD segments if in window, no dynamic linking */
struct eh { uint8_t i[16]; uint16_t type, machine; uint32_t ver, entry, phoff; uint32_t shoff, flags; uint16_t ehsize, phentsize, phnum; } __attribute__((packed));
struct ph { uint32_t type, off, vaddr, paddr, filesz, memsz, flags, align; } __attribute__((packed));

enum dyld_result dyld_load(const uint8_t* image, size_t size, uint32_t* entry_out) {
    if (size < sizeof(struct eh)) return DYLD_ERR_ELF;
    const struct eh* e = (const struct eh*)image;
    if (e->i[0]!=0x7f||e->i[1]!='E'||e->i[2]!='L'||e->i[3]!='F') return DYLD_ERR_ELF;
    if (e->machine != 3) return DYLD_ERR_ELF;
    int loaded = 0;
    for (uint16_t i=0;i<e->phnum;i++) {
        const struct ph* p = (const struct ph*)(image + e->phoff + i*e->phentsize);
        if (p->type != 1) continue;
        uint32_t a = p->vaddr, vend = a + p->memsz;
        int ok = (a >= DYLD_MAIN_BASE && vend <= DYLD_MAIN_BASE+DYLD_MAIN_MAX)
              || (a >= DYLD_LINUX_BASE && vend <= DYLD_LINUX_BASE+DYLD_LINUX_MAX);
        if (!ok) return DYLD_ERR_ELF;
        uint8_t* d = (uint8_t*)(uintptr_t)a;
        for (uint32_t j=0;j<p->filesz;j++) d[j]=image[p->off+j];
        for (uint32_t j=p->filesz;j<p->memsz;j++) d[j]=0;
        loaded = 1;
    }
    if (!loaded) return DYLD_ERR_ELF;
    *entry_out = e->entry;
    terminal_writestring("dyld: simple load OK\n");
    return DYLD_OK;
}

const char* dyld_strerror(enum dyld_result r) {
    switch(r){case DYLD_OK:return "OK";case DYLD_ERR_ELF:return "bad ELF";default:return "err";}
}

void dyld_install_lib_tree(void) {
    /* keep empty - full install needs previous dyld */
}
