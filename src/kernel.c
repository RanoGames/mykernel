/* kernel.c — инициализация подсистем и shell */

#include "gdt.h"
#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "fs.h"
#include "ata.h"
#include "kmalloc.h"
#include "syscall.h"
#include "timer.h"
#include "sched.h"
#include "dyld.h"
#include "shell.h"

void kernel_main(void) {
    gdt_init();
    serial_init();

    terminal_initialize();
    terminal_writestring("Hello, World!\n");
    terminal_writestring("Booting kernel subsystems...\n");

    kmalloc_init();
    fd_table_reset();

    idt_init();
    isr_install();
    irq_install();
    keyboard_init();
    fs_init();
    dyld_install_lib_tree(); /* /lib/libhello.so, /bin/dynhello */
    sched_init();
    timer_init(100);

    if (ata_init() == 0)
        terminal_writestring("ATA: disk detected (use fatmount)\n");
    else
        terminal_writestring("ATA: no disk (optional — make run-fat)\n");

    __asm__ volatile ("sti");

    terminal_writestring("Subsystems ready (dyld + scheduler).\n");

    shell_run();
}
