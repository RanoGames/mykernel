/* mkdraw.h — 2D helpers into an XRGB8888 buffer */

#ifndef MKDRAW_H
#define MKDRAW_H

#include <stdint.h>

void mk_buf_fill(uint32_t* pix, int w, int h, uint32_t color);
void mk_buf_rect(uint32_t* pix, int w, int h, int x, int y, int rw, int rh, uint32_t color);
void mk_buf_hline(uint32_t* pix, int w, int h, int x, int y, int len, uint32_t color);
void mk_buf_vline(uint32_t* pix, int w, int h, int x, int y, int len, uint32_t color);
void mk_buf_char(uint32_t* pix, int w, int h, int x, int y, char c, uint32_t fg, uint32_t bg);
void mk_buf_text(uint32_t* pix, int w, int h, int x, int y, const char* s, uint32_t fg, uint32_t bg);

/* Same helpers drawing directly to VBE LFB */
void mk_fb_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void mk_fb_text(int x, int y, const char* s, uint32_t fg, uint32_t bg);

#endif
