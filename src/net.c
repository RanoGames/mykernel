/* net.c — minimal network facade: loopback ping + status */
#include "net.h"
#include "vga.h"
#include "timer.h"
#include "pci.h"

static int str_eq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int is_loopback(const char* host) {
    return str_eq(host, "127.0.0.1") || str_eq(host, "localhost") || str_eq(host, "::1");
}

void net_init(void) {
    /* PCI scan already elsewhere; placeholder for NIC bind */
}

static void busy_delay(void) {
    for (volatile int i = 0; i < 500000; i++)
        ;
}

int net_ping(const char* host, int count) {
    if (!host || !host[0]) host = "127.0.0.1";
    if (count < 1) count = 4;
    if (count > 10) count = 10;

    terminal_writestring("PING ");
    terminal_writestring(host);
    terminal_writestring("\n");

    if (is_loopback(host)) {
        for (int i = 0; i < count; i++) {
            busy_delay();
            terminal_writestring("  seq=");
            terminal_write_uint((uint32_t)i);
            terminal_writestring("  icmp_seq=");
            terminal_write_uint((uint32_t)i);
            terminal_writestring("  ttl=64  time<1 ms  (loopback)\n");
        }
        terminal_writestring("ping: ");
        terminal_write_uint((uint32_t)count);
        terminal_writestring(" packets  0% loss\n");
        return 0;
    }

    /* No full NIC stack yet — honest message + synthetic demo for gateway */
    terminal_writestring("  note: full Ethernet/IP stack not linked\n");
    terminal_writestring("  (e1000/virtio driver = next step)\n");
    for (int i = 0; i < count; i++) {
        busy_delay();
        terminal_writestring("  seq=");
        terminal_write_uint((uint32_t)i);
        terminal_writestring("  timeout (no NIC TX)\n");
    }
    terminal_writestring("ping: host unreachable — try: ping 127.0.0.1\n");
    return -1;
}
