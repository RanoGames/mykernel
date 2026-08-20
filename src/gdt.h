#ifndef GDT_H
#define GDT_H
#include <stdint.h>

#define GDT_KCODE  0x08
#define GDT_KDATA  0x10
#define GDT_UCODE  0x18
#define GDT_UDATA  0x20
#define GDT_TSS    0x28

void gdt_install(void);
void gdt_init(void); /* same as gdt_install — kernel_main calls this */
void tss_set_kernel_stack(uint32_t esp0);

#endif
