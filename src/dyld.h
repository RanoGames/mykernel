/* dyld.h - simplified in-kernel ELF dynamic loader
 *
 * Understands PT_INTERP, PT_DYNAMIC, DT_NEEDED, basic R_386_* relocations.
 * Not a full ld-linux + glibc - educational minimum.
 */

#ifndef DYLD_H
#define DYLD_H

#include <stdint.h>
#include <stddef.h>

enum dyld_result {
    DYLD_OK = 0,
    DYLD_ERR_ELF,
    DYLD_ERR_INTERP,
    DYLD_ERR_NEEDED,
    DYLD_ERR_RELOC,
    DYLD_ERR_NOSYM,
    DYLD_ERR_NOMEM,
};

/* Load ELF (static or dynamic), return entry point */
enum dyld_result dyld_load(const uint8_t* image, size_t size, uint32_t* entry_out);

const char* dyld_strerror(enum dyld_result r);

/* Install lib blobs into RAM FS */
void dyld_install_lib_tree(void);

#endif
