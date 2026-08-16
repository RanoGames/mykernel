/* vbe.h — Bochs/QEMU VBE linear framebuffer (800x600 и др.) */

#ifndef VBE_H
#define VBE_H

#include <stdint.h>

#define VBE_MODE_640x480   0
#define VBE_MODE_800x600   1
#define VBE_MODE_1024x768  2

struct vbe_info {
    uint32_t lfb;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    int      active;
};

int vbe_probe(void);
int vbe_set_mode(int mode_id);
void vbe_disable(void);
const struct vbe_info* vbe_get_info(void);
int vbe_is_active(void);

void vbe_clear(uint32_t color);
void vbe_putpixel(int x, int y, uint32_t color);
void vbe_fill_rect(int x, int y, int w, int h, uint32_t color);

static inline uint32_t vbe_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void vbe_demo(void);

#endif
