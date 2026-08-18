/* mkdisp.h — минимальный display/compositor (Wayland-inspired, kernel-side) */

#ifndef MKDISP_H
#define MKDISP_H

#include <stdint.h>

#define MK_MAX_SURFACES  8
#define MK_TITLE_H       24
#define MK_PANEL_H       28
#define MK_CURSOR_W      12
#define MK_CURSOR_H      20

struct mk_surface {
    int      used;
    int      x, y, w, h;       /* screen coords (client area origin below title) */
    int      mapped;
    char     title[40];
    uint32_t* pixels;          /* client pixels, XRGB8888, size w*h */
    int      dirty;
};

int  mk_init(int vbe_mode_id);
void mk_shutdown(void);
int  mk_is_active(void);

/* Create surface; returns id >= 0 or -1. Allocates pixel buffer via kmalloc. */
int  mk_surface_create(int w, int h, const char* title);
void mk_surface_destroy(int id);
void mk_surface_move(int id, int x, int y);
void mk_surface_set_title(int id, const char* title);
void mk_surface_commit(int id);          /* mark dirty + compose */
struct mk_surface* mk_surface_get(int id);

void mk_compose(void);                   /* full redraw: wallpaper, panel, windows, cursor */
void mk_set_wallpaper_color(uint32_t c);
void mk_draw_cursor(void);
/* Move cursor without full compose (saves/restores underlay). */
void mk_cursor_move(int x, int y);
void mk_cursor_invalidate(void);

/* Hit-test: topmost surface under (sx,sy), including title bar. Returns id or -1. */
int  mk_hit_test(int sx, int sy, int* local_x, int* local_y, int* on_title, int* on_close);

int  mk_screen_w(void);
int  mk_screen_h(void);

void mk_panel_power_rects(int* reboot_x, int* shut_x, int* by, int* bw, int* bh);

#endif
