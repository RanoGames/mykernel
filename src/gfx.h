/* gfx.h — VGA Mode 13h graphics */

#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define GFX_WIDTH  320
#define GFX_HEIGHT 200

#define GFX_BLACK   0
#define GFX_BLUE    1
#define GFX_GREEN   2
#define GFX_CYAN    3
#define GFX_RED     4
#define GFX_MAGENTA 5
#define GFX_BROWN   6
#define GFX_LGRAY   7
#define GFX_DGRAY   8
#define GFX_LBLUE   9
#define GFX_LGREEN  10
#define GFX_LCYAN   11
#define GFX_LRED    12
#define GFX_LMAGENTA 13
#define GFX_YELLOW  14
#define GFX_WHITE   15

void gfx_init_mode13(void);
void gfx_restore_text(void);
void gfx_clear(uint8_t color);
void gfx_putpixel(int x, int y, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_wait_vsync(void);
void gfx_set_default_palette(void);
int gfx_is_graphics(void);

const uint8_t* gfx_font_glyph(unsigned char c);

#endif
