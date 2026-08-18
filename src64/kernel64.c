/* kernel64.c — minimal x86_64 + Multiboot2 bring-up */

#include <stdint.h>
#include <stddef.h>
#include "paging64.h"

/* VGA text buffer */
static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;
static int row, col;

static void clear(void) {
    for (int i = 0; i < 80 * 25; i++)
        VGA[i] = 0x0F00 | ' ';
    row = col = 0;
}

static void putc(char c) {
    if (c == '\n') {
        col = 0;
        if (++row >= 25) row = 0;
        return;
    }
    VGA[row * 80 + col] = (uint16_t)(0x0F00 | (uint8_t)c);
    if (++col >= 80) {
        col = 0;
        if (++row >= 25) row = 0;
    }
}

static void puts(const char* s) {
    while (*s) putc(*s++);
}

static void put_hex64(uint64_t v) {
    const char* h = "0123456789ABCDEF";
    puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        putc(h[(v >> i) & 0xF]);
}

/* Multiboot2 info tag walk */
struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

extern char _kernel_virtual_start[];
extern char _kernel_virtual_end[];
extern char _kernel_physical_start[];
extern char _kernel_physical_end[];

void kernel_main64(uint32_t magic, uint32_t info) {
    clear();
    puts("MyKernel x86_64 / Multiboot2\n");

    if (magic != 0x36d76289) {
        puts("BAD MAGIC (not Multiboot2)\n");
        puts("EAX was: ");
        put_hex64(magic);
        putc('\n');
        for (;;)
            __asm__ volatile ("hlt");
    }
    puts("Multiboot2 magic OK\n");
    puts("Higher-half VMA start ");
    put_hex64((uint64_t)(uintptr_t)_kernel_virtual_start);
    putc('\n');
    puts("Phys load start      ");
    put_hex64((uint64_t)(uintptr_t)_kernel_physical_start);
    putc('\n');

    puts("Info @ ");
    put_hex64(info);
    putc('\n');

    if (info) {
        uint32_t total = *(uint32_t*)(uintptr_t)info;
        puts("Info size ");
        put_hex64(total);
        putc('\n');

        struct mb2_tag* tag = (struct mb2_tag*)((uintptr_t)info + 8);
        int n = 0;
        while (tag->type != 0 && n < 32) {
            puts("  tag type=");
            put_hex64(tag->type);
            puts(" size=");
            put_hex64(tag->size);
            putc('\n');
            /* next tag: align 8 */
            uint32_t sz = (tag->size + 7u) & ~7u;
            if (sz < 8) break;
            tag = (struct mb2_tag*)((uint8_t*)tag + sz);
            n++;
        }
    }

    paging_apply_w_xor_x();
    paging_dump_pml4();
    puts("\nLong mode + PML4 OK. 32-bit tree: make\n");
    puts("Build: make kernel64 && qemu-system-x86_64 -kernel build64/mykernel64.bin\n");

    for (;;)
        __asm__ volatile ("hlt");
}
