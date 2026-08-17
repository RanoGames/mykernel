/* vbe.c — Bochs VBE (DISPI) + linear framebuffer for QEMU stdvga
 *
 * VirtualBox does NOT implement Bochs DISPI — probe must fail cleanly
 * so callers fall back to Mode 13h / text.
 */

#include "vbe.h"
#include "io.h"
#include "pci.h"
#include "vga.h"
#include "gfx.h"
#include "keyboard.h"
#include <stddef.h>

#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF

#define VBE_DISPI_INDEX_ID           0x0
#define VBE_DISPI_INDEX_XRES         0x1
#define VBE_DISPI_INDEX_YRES         0x2
#define VBE_DISPI_INDEX_BPP          0x3
#define VBE_DISPI_INDEX_ENABLE       0x4
#define VBE_DISPI_INDEX_BANK         0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH   0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT  0x7
#define VBE_DISPI_INDEX_X_OFFSET     0x8
#define VBE_DISPI_INDEX_Y_OFFSET     0x9

#define VBE_DISPI_DISABLED     0x00
#define VBE_DISPI_ENABLED      0x01
#define VBE_DISPI_LFB_ENABLED  0x40

#define VBE_DISPI_ID0 0xB0C0
#define VBE_DISPI_ID5 0xB0C5

static struct vbe_info g_vbe;
static int g_probed;
static int g_dispi_ok; /* 1 only if Bochs DISPI ID readback is valid */

static void dispi_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t dispi_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static uint32_t find_lfb_pci(void) {
    struct pci_device dev;
    /* QEMU stdvga / bochs-display */
    if (pci_find_device(0x1234, 0x1111, &dev)) {
        uint32_t bar = dev.bar[0] & ~0xFu;
        if (bar) return bar;
    }
    if (pci_find_by_class(0x03, 0x00, &dev)) {
        uint32_t bar = dev.bar[0] & ~0xFu;
        if (bar) return bar;
    }
    return 0; /* no guess — VirtualBox BAR differs; without DISPI useless */
}

int vbe_probe(void) {
    g_vbe.lfb = 0;
    g_vbe.width = 0;
    g_vbe.height = 0;
    g_vbe.bpp = 32;
    g_vbe.pitch = 0;
    g_vbe.active = 0;
    g_dispi_ok = 0;
    g_probed = 1;

    /* Detect Bochs/QEMU DISPI: write ID5, read back must stay in B0C0..B0C5 */
    dispi_write(VBE_DISPI_INDEX_ID, VBE_DISPI_ID5);
    uint16_t id = dispi_read(VBE_DISPI_INDEX_ID);
    if (id < VBE_DISPI_ID0 || id > VBE_DISPI_ID5) {
        /* VirtualBox and most real HW: ports ignored / 0xFFFF / garbage */
        return 0;
    }

    g_vbe.lfb = find_lfb_pci();
    if (!g_vbe.lfb) {
        /* DISPI present but no LFB BAR — still unusable for us */
        return 0;
    }

    g_dispi_ok = 1;
    return 1;
}

int vbe_set_mode(int mode_id) {
    uint32_t w, h;
    switch (mode_id) {
        case VBE_MODE_640x480:  w = 640;  h = 480;  break;
        case VBE_MODE_800x600:  w = 800;  h = 600;  break;
        case VBE_MODE_1024x768: w = 1024; h = 768;  break;
        default:                w = 800;  h = 600;  break;
    }

    if (!g_probed && !vbe_probe())
        return -1;
    if (!g_dispi_ok) {
        /* probe was called earlier and failed, or never succeeded */
        if (!vbe_probe())
            return -1;
    }
    if (!g_vbe.lfb)
        g_vbe.lfb = find_lfb_pci();
    if (!g_vbe.lfb || !g_dispi_ok)
        return -1;

    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    dispi_write(VBE_DISPI_INDEX_XRES, (uint16_t)w);
    dispi_write(VBE_DISPI_INDEX_YRES, (uint16_t)h);
    dispi_write(VBE_DISPI_INDEX_BPP, 32);
    dispi_write(VBE_DISPI_INDEX_VIRT_WIDTH, (uint16_t)w);
    dispi_write(VBE_DISPI_INDEX_VIRT_HEIGHT, (uint16_t)h);
    dispi_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    dispi_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    dispi_write(VBE_DISPI_INDEX_ENABLE,
                (uint16_t)(VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED));

    /* Verify hardware accepted the mode (VirtualBox will not) */
    uint16_t rw = dispi_read(VBE_DISPI_INDEX_XRES);
    uint16_t rh = dispi_read(VBE_DISPI_INDEX_YRES);
    uint16_t en = dispi_read(VBE_DISPI_INDEX_ENABLE);
    if (rw != (uint16_t)w || rh != (uint16_t)h || !(en & VBE_DISPI_ENABLED)) {
        dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        g_vbe.active = 0;
        return -1;
    }

    g_vbe.width = w;
    g_vbe.height = h;
    g_vbe.bpp = 32;
    g_vbe.pitch = w * 4;
    g_vbe.active = 1;
    return 0;
}

void vbe_disable(void) {
    if (g_dispi_ok)
        dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    g_vbe.active = 0;
}

const struct vbe_info* vbe_get_info(void) { return &g_vbe; }
int vbe_is_active(void) { return g_vbe.active; }

void vbe_clear(uint32_t color) {
    if (!g_vbe.active || !g_vbe.lfb) return;
    volatile uint32_t* fb = (volatile uint32_t*)(uint32_t)g_vbe.lfb;
    uint32_t n = g_vbe.width * g_vbe.height;
    for (uint32_t i = 0; i < n; i++) fb[i] = color;
}

void vbe_putpixel(int x, int y, uint32_t color) {
    if (!g_vbe.active || !g_vbe.lfb) return;
    if ((unsigned)x >= g_vbe.width || (unsigned)y >= g_vbe.height) return;
    volatile uint32_t* fb = (volatile uint32_t*)(uint32_t)g_vbe.lfb;
    fb[y * g_vbe.width + x] = color;
}

void vbe_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0 || !g_vbe.active) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= g_vbe.height) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if ((unsigned)xx >= g_vbe.width) continue;
            vbe_putpixel(xx, yy, color);
        }
    }
}

void vbe_demo(void) {
    terminal_writestring("VBE: probe...\n");
    if (!vbe_probe()) {
        terminal_writestring("VBE: DISPI not available (ok on VirtualBox)\n");
        return;
    }
    terminal_writestring("VBE: setting 800x600x32...\n");
    if (vbe_set_mode(VBE_MODE_800x600) != 0) {
        terminal_writestring("VBE: set_mode failed\n");
        return;
    }
    const struct vbe_info* vi = vbe_get_info();
    terminal_writestring("VBE: LFB=");
    terminal_write_hex(vi->lfb);
    terminal_writestring(" ");
    terminal_write_uint(vi->width);
    terminal_writestring("x");
    terminal_write_uint(vi->height);
    terminal_putchar('\n');

    vbe_clear(0x00000020);
    for (int i = 0; i < 8; i++) {
        uint32_t c = vbe_rgb((uint8_t)(i * 32), (uint8_t)(255 - i * 20), 128);
        vbe_fill_rect(40 + i * 80, 80, 60, 60, c);
    }
    /* wait key */
    terminal_writestring("VBE demo: press any key to restore text\n");
    (void)keyboard_getchar();
    vbe_disable();
    gfx_restore_text();
}
