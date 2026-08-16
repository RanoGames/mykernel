/* gfx.h — простая графика VGA Mode 13h (320x200x256) */

#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define GFX_WIDTH  320
#define GFX_HEIGHT 200

/* палитра (стандартные индексы VGA) */
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

void gfx_init_mode13(void);   /* войти в графику */
void gfx_restore_text(void);  /* назад в текстовый 80x25 */

void gfx_clear(uint8_t color);
void gfx_putpixel(int x, int y, uint8_t color);
uint8_t gfx_getpixel(int x, int y);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);

int gfx_is_graphics(void);

#endif
