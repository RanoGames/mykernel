/* gfx.c — VGA Mode 13h */

#include "gfx.h"
#include "io.h"
#include "vga.h"
#include <stdint.h>

#define VGA_VRAM ((volatile uint8_t*)0xA0000)

static int g_graphics;

void gfx_set_default_palette(void) {
    /* 6-bit RGB DAC: R,G,B в диапазоне 0..63 */
    static const uint8_t pal[16][3] = {
        { 0,  0,  0}, /* black */
        { 0,  0, 42}, /* blue */
        { 0, 42,  0}, /* green */
        { 0, 42, 42}, /* cyan */
        {42,  0,  0}, /* red */
        {42,  0, 42}, /* magenta */
        {42, 21,  0}, /* brown */
        {42, 42, 42}, /* light gray */
        {21, 21, 21}, /* dark gray */
        {21, 21, 63}, /* light blue */
        {21, 63, 21}, /* light green */
        {21, 63, 63}, /* light cyan */
        {63, 21, 21}, /* light red */
        {63, 21, 63}, /* light magenta */
        {63, 63, 21}, /* yellow */
        {63, 63, 63}, /* white */
    };
    outb(0x3C8, 0);
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, pal[i][0]);
        outb(0x3C9, pal[i][1]);
        outb(0x3C9, pal[i][2]);
    }
}

void gfx_wait_vsync(void) {
    /* ждать конца кадра: bit3 порта 0x3DA */
    while (inb(0x3DA) & 0x08)
        ;
    while (!(inb(0x3DA) & 0x08))
        ;
}

void gfx_init_mode13(void) {
    outb(0x3C2, 0x63);

    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E);

    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);

    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF
    };
    for (uint8_t i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);

    inb(0x3DA);
    for (uint8_t i = 0; i < 16; i++) {
        outb(0x3C0, i);
        outb(0x3C0, i);
    }
    outb(0x3C0, 0x10); outb(0x3C0, 0x41);
    outb(0x3C0, 0x11); outb(0x3C0, 0x00);
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F);
    outb(0x3C0, 0x13); outb(0x3C0, 0x00);
    outb(0x3C0, 0x14); outb(0x3C0, 0x00);
    outb(0x3C0, 0x20);

    gfx_set_default_palette();
    g_graphics = 1;
    gfx_clear(GFX_BLACK);
}

void gfx_restore_text(void) {
    outb(0x3C2, 0x67);

    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x00);
    outb(0x3C4, 0x02); outb(0x3C5, 0x03);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);

    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);

    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
        0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
        0xFF
    };
    for (uint8_t i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
    outb(0x3CE, 0x07); outb(0x3CF, 0x00);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);

    inb(0x3DA);
    for (uint8_t i = 0; i < 16; i++) {
        outb(0x3C0, i);
        outb(0x3C0, i);
    }
    outb(0x3C0, 0x10); outb(0x3C0, 0x0C);
    outb(0x3C0, 0x11); outb(0x3C0, 0x00);
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F);
    outb(0x3C0, 0x13); outb(0x3C0, 0x08);
    outb(0x3C0, 0x14); outb(0x3C0, 0x00);
    outb(0x3C0, 0x20);

    g_graphics = 0;
    terminal_initialize();
}

void gfx_clear(uint8_t color) {
    volatile uint8_t* v = VGA_VRAM;
    for (int i = 0; i < GFX_WIDTH * GFX_HEIGHT; i++)
        v[i] = color;
}

void gfx_putpixel(int x, int y, uint8_t color) {
    if ((unsigned)x >= GFX_WIDTH || (unsigned)y >= GFX_HEIGHT)
        return;
    VGA_VRAM[y * GFX_WIDTH + x] = color;
}

uint8_t gfx_getpixel(int x, int y) {
    if ((unsigned)x >= GFX_WIDTH || (unsigned)y >= GFX_HEIGHT)
        return 0;
    return VGA_VRAM[y * GFX_WIDTH + x];
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= GFX_HEIGHT) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if ((unsigned)xx >= GFX_WIDTH) continue;
            VGA_VRAM[yy * GFX_WIDTH + xx] = color;
        }
    }
}

void gfx_hline(int x, int y, int w, uint8_t color) {
    gfx_fill_rect(x, y, w, 1, color);
}

void gfx_vline(int x, int y, int h, uint8_t color) {
    gfx_fill_rect(x, y, 1, h, color);
}

int gfx_is_graphics(void) {
    return g_graphics;
}
