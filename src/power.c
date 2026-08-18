#include "power.h"
#include "io.h"
#include "vga.h"

void machine_reboot(void) {
    terminal_writestring("Rebooting...\n");
    /* Wait for keyboard controller input buffer clear, then pulse reset */
    uint8_t st;
    do {
        st = inb(0x64);
    } while (st & 0x02);
    outb(0x64, 0xFE);
    for (;;)
        __asm__ volatile ("hlt");
}

void machine_shutdown(void) {
    terminal_writestring("Shutting down...\n");
    /* QEMU/Bochs: port 0x604 ACPI PM1a */
    outw(0x604, 0x2000);
    /* Fallback: keyboard reset if host ignores ACPI */
    outb(0x64, 0xFE);
    for (;;)
        __asm__ volatile ("hlt");
}
