/* kernel.c */

#include "gdt.h"
#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "mouse.h"
#include "fs.h"
#include "vfs.h"
#include "net.h"
#include "ata.h"
#include "kmalloc.h"
#include "syscall.h"
#include "timer.h"
#include "platform.h"
#include "sched.h"
#include "dyld.h"
#include "pci.h"
#include "ac97.h"
#include "shell.h"
#include "settings.h"
#include "vbe.h"
#include "desktop.h"

void kernel_main(void) {
    gdt_init();
    serial_init();

    terminal_initialize();
    terminal_writestring("MyKernel booting...\n");
    terminal_writestring("Booting kernel subsystems...\n");

    kmalloc_init();
    fd_table_reset();

    idt_init();
    isr_install();
    irq_install();
    keyboard_init();
    mouse_init();
    platform_init();
    fs_init();
    vfs_init();
    net_init();
    dyld_install_lib_tree();
    sched_init();
    timer_init(100); /* PIT IRQ0 @ 100 Hz (10 ms/tick) */

    if (ata_init() == 0) {
        terminal_writestring("ATA: disk detected\n");
        if (vfs_auto_mount() == 0) {
            terminal_writestring("VFS: FAT32 auto-mounted at /mnt/c and /c\n");
        }
    } else {
        terminal_writestring("ATA: no disk\n");
    }

    __asm__ volatile ("sti");

    ac97_init();
    settings_init();
    vbe_probe();

    /* Boot directly into graphical desktop environment by default */
    desktop_run();

    terminal_writestring("Desktop exited. Falling back to native shell.\n");
    terminal_writestring("Type 'desktop' to return to graphical desktop environment.\n");
    shell_run();
}
