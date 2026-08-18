#include "platform.h"
#include "pci.h"
#include "vbe.h"

static int g_vbox;
static int g_inited;

static int pci_has_vendor(uint16_t vendor) {
    for (uint8_t bus = 0; bus < 2; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t fn = 0; fn < 8; fn++) {
                uint16_t v = pci_config_read16(bus, dev, fn, 0x00);
                if (v == vendor)
                    return 1;
                if (fn == 0) {
                    uint8_t ht = (uint8_t)(pci_config_read16(bus, dev, fn, 0x0E) & 0xFF);
                    if ((ht & 0x80) == 0)
                        break; /* not multi-function */
                }
            }
        }
    }
    return 0;
}

void platform_init(void) {
    g_vbox = pci_has_vendor(0x80EE); /* Innotek/Oracle VirtualBox */
    g_inited = 1;
}

int platform_is_virtualbox(void) {
    if (!g_inited) platform_init();
    return g_vbox;
}

int platform_has_vbe(void) {
    if (platform_is_virtualbox())
        return 0; /* Bochs DISPI not available in VBox */
    return vbe_probe();
}
