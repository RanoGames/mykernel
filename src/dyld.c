/* dyld.c - restored, see git history 2a5e350 for full; temporary stub will be replaced */
#include "dyld.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

enum dyld_result dyld_load(const uint8_t* image, size_t size, uint32_t* entry_out) {
    (void)image; (void)size; (void)entry_out;
    terminal_writestring("dyld: temporarily broken, pull again soon\n");
    return DYLD_ERR_ELF;
}

const char* dyld_strerror(enum dyld_result r) {
    (void)r;
    return "dyld temporarily stubbed";
}

void dyld_install_lib_tree(void) {
    terminal_writestring("dyld: install stub\n");
}
