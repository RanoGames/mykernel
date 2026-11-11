/* process.c — exec ELF через dyld; возврат в shell через setjmp/longjmp */

#include "process.h"
#include "gdt.h"
#include "elf.h"
#include "dyld.h"
#include "syscall.h"
#include "vga.h"
#include <stdint.h>
#include <stddef.h>

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
    if (!image || size < 52) return 0;
    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F')
        return 0;
    uint16_t phentsize = *(const uint16_t*)(image + 42);
    uint16_t phnum = *(const uint16_t*)(image + 44);
    uint32_t phoff = *(const uint32_t*)(image + 28);
    if (phentsize < 32) return 0;
    if (phoff >= size) return 0;
    if ((uint64_t)phoff + (uint64_t)phnum * phentsize > (uint64_t)size)
        return 0;
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t* ph = image + phoff + (size_t)i * phentsize;
        uint32_t p_type = *(const uint32_t*)ph;
        if (p_type == 3 || p_type == 2)
            return 1;
    }
    return 0;
}

int process_exec(const uint8_t* image, size_t size) {
    if (!image || size == 0) {
        terminal_writestring("exec: invalid image buffer\n");
        return -1;
    }

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

    if (entry == 0) {
        terminal_writestring("exec: entry point is NULL\n");
        return -1;
    }

    user_heap_reset();
    fd_table_reset();

    terminal_writestring("exec: entry=");
    terminal_write_hex(entry);
    terminal_putchar('\n');

    g_exit_code = 0;
    g_have_jmp = 1;

    if (process_setjmp(g_jmpbuf) == 0) {
        /* Build Linux / System V ABI stack frame for glibc & musl */
        char* str_ptr = (char*)(ELF_LOAD_BASE + ELF_LOAD_MAX - 256);
        char* arg0 = str_ptr;
        const char* a0 = "program";
        while (*a0) *str_ptr++ = *a0++;
        *str_ptr++ = '\0';

        char* env0 = str_ptr;
        const char* e0 = "PATH=/bin";
        while (*e0) *str_ptr++ = *e0++;
        *str_ptr++ = '\0';

        /* 16 random bytes for AT_RANDOM (used by glibc for stack canary & pointer mangle) */
        uint8_t* rand_bytes = (uint8_t*)str_ptr;
        for (int i = 0; i < 16; i++)
            *str_ptr++ = (char)(0x42 + i * 7);

        /* Push vector elements onto stack:
         * argc, argv[], envp[], and AUXV (AT_PAGESZ, AT_RANDOM, AT_ENTRY, AT_CLKTCK, AT_NULL) */
        uint32_t* sp = (uint32_t*)(ELF_LOAD_BASE + ELF_LOAD_MAX - 512);
        sp = (uint32_t*)((uint32_t)sp & ~0xFu); /* 16-byte align */
        int s_idx = 0;
        sp[s_idx++] = 1;                         /* argc */
        sp[s_idx++] = (uint32_t)(uintptr_t)arg0; /* argv[0] */
        sp[s_idx++] = 0;                         /* NULL */
        sp[s_idx++] = (uint32_t)(uintptr_t)env0; /* envp[0] */
        sp[s_idx++] = 0;                         /* NULL */

        /* AUXV */
        sp[s_idx++] = 6;                         /* AT_PAGESZ */
        sp[s_idx++] = 4096;
        sp[s_idx++] = 25;                        /* AT_RANDOM */
        sp[s_idx++] = (uint32_t)(uintptr_t)rand_bytes;
        sp[s_idx++] = 9;                         /* AT_ENTRY */
        sp[s_idx++] = entry;
        sp[s_idx++] = 17;                        /* AT_CLKTCK */
        sp[s_idx++] = 100;
        sp[s_idx++] = 0;                         /* AT_NULL */
        sp[s_idx++] = 0;

        uint32_t user_stack = (uint32_t)(uintptr_t)sp;
        static uint8_t ring0_stack[8192] __attribute__((aligned(16)));
        tss_set_kernel_stack((uint32_t)(ring0_stack + sizeof(ring0_stack)));

        uint32_t user_cs = 0x1B;
        uint32_t user_ss = 0x23;
        uint32_t eflags  = 0x202;

        __asm__ volatile(
            "mov $0x23, %%ax\n\t"
            "mov %%ax, %%ds\n\t"
            "mov %%ax, %%es\n\t"
            "mov %%ax, %%fs\n\t"
            "mov %%ax, %%gs\n\t"
            "pushl %0\n\t"
            "pushl %1\n\t"
            "pushl %2\n\t"
            "pushl %3\n\t"
            "pushl %4\n\t"
            "iret\n\t"
            :
            : "r"(user_ss), "r"(user_stack), "r"(eflags), "r"(user_cs), "r"(entry)
            : "memory", "eax"
        );
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
