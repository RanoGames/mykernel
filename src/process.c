/* process.c — exec ELF через dyld; возврат в shell через setjmp/longjmp */

#include "process.h"
#include "elf.h"
#include "dyld.h"
#include "syscall.h"
#include "vga.h"
#include <stdint.h>

/* jmp_buf: ebx,esi,edi,ebp,esp,eip */
static uint32_t g_jmpbuf[6];
static int g_exit_code;
static int g_have_jmp;

extern int process_setjmp(uint32_t* buf);
extern void process_longjmp(uint32_t* buf, int val);

void process_save_kernel_context(uint32_t esp, uint32_t ebp, void* cont) {
    (void)esp; (void)ebp; (void)cont;
    /* kept for API compat; real save is process_setjmp in process_exec */
}

int process_last_exit_code(void) {
    return g_exit_code;
}

void process_exit_to_kernel(int code) {
    g_exit_code = code;
    if (!g_have_jmp) {
        terminal_writestring("exit: no jmp context\n");
        for (;;) __asm__ volatile ("hlt");
    }
    /* Does not return — jumps back to process_setjmp site in process_exec */
    process_longjmp(g_jmpbuf, 1);
}

static int elf_is_dynamic(const uint8_t* image, size_t size) {
    if (size < 52) return 0;
    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F')
        return 0;
    uint16_t phnum = *(const uint16_t*)(image + 44);
    uint32_t phoff = *(const uint32_t*)(image + 28);
    uint16_t phentsize = *(const uint16_t*)(image + 42);
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t* ph = image + phoff + i * phentsize;
        uint32_t p_type = *(const uint32_t*)ph;
        if (p_type == 3 || p_type == 2)
            return 1;
    }
    return 0;
}

int process_exec(const uint8_t* image, size_t size) {
    uint32_t entry = 0;
    int is_dyn = elf_is_dynamic(image, size);

    enum dyld_result dr = dyld_load(image, size, &entry);
    if (dr != DYLD_OK) {
        if (is_dyn) {
            terminal_writestring("dyld failed: ");
            terminal_writestring(dyld_strerror(dr));
            terminal_putchar('\n');
            return -1;
        }
        enum elf_result er = elf_load(image, size, &entry);
        if (er != ELF_OK) {
            terminal_writestring("dyld: ");
            terminal_writestring(dyld_strerror(dr));
            terminal_writestring(" / elf: ");
            terminal_writestring(elf_strerror(er));
            terminal_putchar('\n');
            return -1;
        }
    }

    user_heap_reset();
    fd_table_reset();

    terminal_writestring("exec: entry=");
    terminal_write_hex(entry);
    terminal_putchar('\n');

    g_exit_code = 0;
    g_have_jmp = 1;

    if (process_setjmp(g_jmpbuf) == 0) {
        /* First return from setjmp: jump into userspace */
        uint32_t user_stack = ELF_LOAD_BASE + ELF_LOAD_MAX - 16;
        __asm__ volatile(
            "mov %0, %%esp\n"
            "xor %%ebp, %%ebp\n"
            "jmp *%1\n"
            :
            : "r"(user_stack), "r"(entry)
            : "memory"
        );
        /* never reached */
        for (;;) __asm__ volatile ("hlt");
    }

    /* Second return: from process_longjmp in sys_exit */
    g_have_jmp = 0;
    __asm__ volatile ("sti");
    terminal_writestring("exec: done, code=");
    terminal_write_int(g_exit_code);
    terminal_putchar('\n');
    return g_exit_code;
}
