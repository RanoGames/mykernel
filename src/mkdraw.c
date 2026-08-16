#include "mkdraw.h"
#include "gfx.h"
#include "vbe.h"

static void put(uint32_t* pix, int w, int h, int x, int y, uint32_t c) {
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) return;
    pix[y * w + x] = c;
}

void mk_buf_fill(uint32_t* pix, int w, int h, uint32_t color) {
    int n = w * h;
    for (int i = 0; i < n; i++) pix[i] = color;
}

void mk_buf_rect(uint32_t* pix, int w, int h, int x, int y, int rw, int rh, uint32_t color) {
    if (rw <= 0 || rh <= 0) return;
    for (int j = 0; j < rh; j++)
        for (int i = 0; i < rw; i++)
            put(pix, w, h, x + i, y + j, color);
}

void mk_buf_hline(uint32_t* pix, int w, int h, int x, int y, int len, uint32_t color) {
    mk_buf_rect(pix, w, h, x, y, len, 1, color);
}

void mk_buf_vline(uint32_t* pix, int w, int h, int x, int y, int len, uint32_t color) {
    mk_buf_rect(pix, w, h, x, y, 1, len, color);
}

void mk_buf_char(uint32_t* pix, int w, int h, int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t* g = gfx_font_glyph((unsigned char)c);
    for (int row = 0; row < 16; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            put(pix, w, h, x + col, y + row, color);
        }
    }
}

void mk_buf_text(uint32_t* pix, int w, int h, int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (s && *s) {
        if (*s == '\n') { cx = x; y += 16; s++; continue; }
        mk_buf_char(pix, w, h, cx, y, *s, fg, bg);
        cx += 8;
        s++;
    }
}

void mk_fb_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t* g = gfx_font_glyph((unsigned char)c);
    for (int row = 0; row < 16; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            vbe_putpixel(x + col, y + row, color);
        }
    }
}

void mk_fb_text(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (s && *s) {
        if (*s == '\n') { cx = x; y += 16; s++; continue; }
        mk_fb_char(cx, y, *s, fg, bg);
        cx += 8;
        s++;
    }
}
