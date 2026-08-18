#ifndef GDT_H
#define GDT_H
#include <stdint.h>

/* Selectors */
#define GDT_KCODE  0x08
#define GDT_KDATA  0x10
#define GDT_UCODE  0x18  /* RPL will be | 3 → 0x1B */
#define GDT_UDATA  0x20  /* RPL | 3 → 0x23 */
#define GDT_TSS    0x28

void gdt_install(void);
void tss_set_kernel_stack(uint32_t esp0);

#endif
