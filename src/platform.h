#ifndef PLATFORM_H
#define PLATFORM_H

/* 1 if VirtualBox PCI devices detected (vendor 0x80EE) */
int platform_is_virtualbox(void);
/* 1 if Bochs/QEMU VBE DISPI is usable */
int platform_has_vbe(void);
void platform_init(void);

#endif
