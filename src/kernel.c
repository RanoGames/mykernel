/* kernel.c */

#include "gdt.h"
#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "mouse.h"
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
    mouse_init();
    fs_init();
    dyld_install_lib_tree();
    sched_init();
    timer_init(100);

    if (ata_init() == 0)
        terminal_writestring("ATA: disk detected\n");
    else
        terminal_writestring("ATA: no disk\n");

    __asm__ volatile ("sti");

    terminal_writestring("Ready. Type 'snake' for game, 'help' for commands.\n");
    shell_run();
}
